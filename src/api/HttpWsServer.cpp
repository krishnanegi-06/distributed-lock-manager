#include "dlm/api/HttpWsServer.h"
#include "dlm/common/Hash.h"
#include "dlm/common/Logger.h"
#include "dlm/common/Platform.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace dlm {

namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            try {
                int v = std::stoi(s.substr(i + 1, 2), nullptr, 16);
                out.push_back(static_cast<char>(v));
                i += 2;
                continue;
            } catch (...) {}
        } else if (s[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

const char* statusReason(int code) {
    switch (code) {
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "OK";
    }
}

} // namespace

HttpWsServer::HttpWsServer(std::string host, uint16_t port, std::vector<std::string> allowedOrigins)
    : host_(std::move(host)), port_(port), allowedOrigins_(std::move(allowedOrigins)) {
#ifdef _WIN32
    WSADATA wsaData{};
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

HttpWsServer::~HttpWsServer() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

void HttpWsServer::setRequestHandler(RequestHandler handler) {
    requestHandler_ = std::move(handler);
}

void HttpWsServer::setWsAuthHandler(WsAuthHandler handler) {
    wsAuthHandler_ = std::move(handler);
}

void HttpWsServer::closeSock(ApiSocket sock) {
    if (sock == kInvalidApiSocket) return;
#ifdef _WIN32
    ::shutdown(sock, SD_BOTH);
    ::closesocket(sock);
#else
    ::shutdown(sock, SHUT_RDWR);
    ::close(sock);
#endif
}

bool HttpWsServer::sendAll(ApiSocket sock, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(sock, data + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool HttpWsServer::recvUntil(ApiSocket sock, std::string& outBuf, const std::string& delim, size_t maxBytes) {
    char tmp[2048];
    while (outBuf.find(delim) == std::string::npos) {
        if (outBuf.size() >= maxBytes) return false;
        int n = ::recv(sock, tmp, sizeof(tmp), 0);
        if (n <= 0) return false;
        outBuf.append(tmp, static_cast<size_t>(n));
    }
    return true;
}

bool HttpWsServer::recvExact(ApiSocket sock, std::string& outBuf, size_t needed) {
    char tmp[4096];
    while (outBuf.size() < needed) {
        size_t toRead = std::min(sizeof(tmp), needed - outBuf.size());
        int n = ::recv(sock, tmp, static_cast<int>(toRead), 0);
        if (n <= 0) return false;
        outBuf.append(tmp, static_cast<size_t>(n));
    }
    return true;
}

bool HttpWsServer::isOriginAllowed(const std::string& origin) const {
    if (origin.empty()) return false;
    for (const auto& allowed : allowedOrigins_) {
        if (allowed == "*" || allowed == origin) return true;
    }
    return false;
}

bool HttpWsServer::checkRateLimit(const std::string& clientIp,
                                  const std::string& bucket,
                                  size_t maxPerMinute) {
    std::string key = clientIp + "|" + bucket;
    uint64_t now = nowEpochMs();
    uint64_t windowStart = (now > 60000ULL) ? (now - 60000ULL) : 0ULL;

    std::lock_guard<std::mutex> lock(rateMutex_);
    auto& q = rateBuckets_[key];
    while (!q.empty() && q.front() < windowStart) {
        q.pop_front();
    }
    if (q.size() >= maxPerMinute) {
        return false;
    }
    q.push_back(now);
    return true;
}

bool HttpWsServer::start() {
    if (running_.load()) return true;

    serverSock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSock_ == kInvalidApiSocket) return false;

    int opt = 1;
    setsockopt(serverSock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (host_.empty() || host_ == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
    }

    if (::bind(serverSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSock(serverSock_);
        serverSock_ = kInvalidApiSocket;
        return false;
    }

    if (::listen(serverSock_, SOMAXCONN) != 0) {
        closeSock(serverSock_);
        serverSock_ = kInvalidApiSocket;
        return false;
    }

    running_.store(true);
    acceptThread_ = std::thread(&HttpWsServer::acceptLoop, this);
    return true;
}

void HttpWsServer::stop() {
    if (!running_.exchange(false)) return;

    if (serverSock_ != kInvalidApiSocket) {
        closeSock(serverSock_);
        serverSock_ = kInvalidApiSocket;
    }

    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        wsClients_.clear();
    }

    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        for (ApiSocket s : activeSockets_) {
            closeSock(s);
        }
        activeSockets_.clear();
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    std::vector<std::unique_ptr<Worker>> ws;
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        ws.swap(workers_);
    }
    for (auto& w : ws) {
        if (w->thread.joinable()) w->thread.join();
    }
}

void HttpWsServer::reapWorkers() {
    std::vector<std::unique_ptr<Worker>> finished;
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        for (auto it = workers_.begin(); it != workers_.end(); ) {
            if ((*it)->done.load()) {
                finished.push_back(std::move(*it));
                it = workers_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& w : finished) {
        if (w->thread.joinable()) w->thread.join();
    }
}

void HttpWsServer::acceptLoop() {
    while (running_.load()) {
        sockaddr_in cliAddr{};
#ifdef _WIN32
        int cliLen = sizeof(cliAddr);
#else
        socklen_t cliLen = sizeof(cliAddr);
#endif
        ApiSocket cliSock = ::accept(serverSock_, reinterpret_cast<sockaddr*>(&cliAddr), &cliLen);
        if (cliSock == kInvalidApiSocket) break;

        char ipBuf[INET_ADDRSTRLEN] = "127.0.0.1";
        inet_ntop(AF_INET, &cliAddr.sin_addr, ipBuf, sizeof(ipBuf));
        std::string clientIp(ipBuf);

        reapWorkers();

        auto w = std::make_unique<Worker>();
        Worker* rawW = w.get();
        std::lock_guard<std::mutex> lock(workerMutex_);
        activeSockets_.insert(cliSock);
        w->thread = std::thread([this, cliSock, clientIp, rawW]() {
            handleConnection(cliSock, clientIp);
            rawW->done.store(true);
        });
        workers_.push_back(std::move(w));
    }
}

void HttpWsServer::handleConnection(ApiSocket sock, std::string clientIp) {
#ifdef _WIN32
    DWORD tv = 15000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv{15, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::string buf;
    if (!recvUntil(sock, buf, "\r\n\r\n", kMaxHeaderBytes)) {
        std::lock_guard<std::mutex> lock(workerMutex_);
        activeSockets_.erase(sock);
        closeSock(sock);
        return;
    }

    size_t hdrEnd = buf.find("\r\n\r\n");
    std::string headerPart = buf.substr(0, hdrEnd);
    std::string leftover = buf.substr(hdrEnd + 4);

    std::istringstream iss(headerPart);
    std::string reqLine;
    if (!std::getline(iss, reqLine)) {
        closeSock(sock);
        return;
    }
    if (!reqLine.empty() && reqLine.back() == '\r') reqLine.pop_back();

    std::istringstream rl(reqLine);
    HttpRequest req;
    std::string version;
    rl >> req.method >> req.rawTarget >> version;
    req.clientIp = clientIp;
    req.requestId = randomHex(8);

    auto qPos = req.rawTarget.find('?');
    if (qPos == std::string::npos) {
        req.path = req.rawTarget;
    } else {
        req.path = req.rawTarget.substr(0, qPos);
        req.query = req.rawTarget.substr(qPos + 1);
        std::istringstream qss(req.query);
        std::string kv;
        while (std::getline(qss, kv, '&')) {
            auto eq = kv.find('=');
            if (eq != std::string::npos) {
                req.queryParams[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
            }
        }
    }

    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto col = line.find(':');
        if (col != std::string::npos) {
            req.headers[toLower(trim(line.substr(0, col)))] = trim(line.substr(col + 1));
        }
    }

    if (req.headers.count("x-request-id") && !req.headers["x-request-id"].empty()) {
        req.requestId = req.headers["x-request-id"].substr(0, 64);
    }

    // Check WebSocket upgrade
    std::string upgradeHdr = toLower(req.headers["upgrade"]);
    if (req.method == "GET" && upgradeHdr == "websocket" && req.path == "/ws") {
        bool authorized = wsAuthHandler_ ? wsAuthHandler_(req) : true;
        std::string wsKey = req.headers["sec-websocket-key"];
        if (!authorized || wsKey.empty()) {
            std::string resp =
                "HTTP/1.1 401 Unauthorized\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: 27\r\n"
                "Connection: close\r\n\r\n"
                "{\"error\":\"Unauthorized WS\"}";
            sendAll(sock, resp.data(), resp.size());
            std::lock_guard<std::mutex> lock(workerMutex_);
            activeSockets_.erase(sock);
            closeSock(sock);
            return;
        }

        // Compute RFC-6455 Sec-WebSocket-Accept
        std::string acceptSeed = wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        auto digest = sha1(acceptSeed);
        std::string acceptVal = base64Encode(digest.data(), digest.size());

        std::ostringstream hs;
        hs << "HTTP/1.1 101 Switching Protocols\r\n"
           << "Upgrade: websocket\r\n"
           << "Connection: Upgrade\r\n"
           << "Sec-WebSocket-Accept: " << acceptVal << "\r\n"
           << "X-Request-Id: " << req.requestId << "\r\n\r\n";
        std::string hsStr = hs.str();
        if (sendAll(sock, hsStr.data(), hsStr.size())) {
            handleWebSocket(sock);
        }
        std::lock_guard<std::mutex> lock(workerMutex_);
        activeSockets_.erase(sock);
        closeSock(sock);
        return;
    }

    // Read Content-Length body if present
    size_t contentLen = 0;
    if (req.headers.count("content-length")) {
        try {
            contentLen = std::stoull(req.headers["content-length"]);
        } catch (...) {
            contentLen = kMaxBodyBytes + 1;
        }
    }

    if (contentLen > kMaxBodyBytes) {
        std::string body = "{\"error\":\"Payload too large\"}";
        std::ostringstream resp;
        resp << "HTTP/1.1 413 Payload Too Large\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n" << body;
        std::string rstr = resp.str();
        sendAll(sock, rstr.data(), rstr.size());
        std::lock_guard<std::mutex> lock(workerMutex_);
        activeSockets_.erase(sock);
        closeSock(sock);
        return;
    }

    if (contentLen > 0) {
        if (!recvExact(sock, leftover, contentLen)) {
            std::lock_guard<std::mutex> lock(workerMutex_);
            activeSockets_.erase(sock);
            closeSock(sock);
            return;
        }
        req.body = leftover.substr(0, contentLen);
    }

    std::string origin = req.headers.count("origin") ? req.headers["origin"] : "";
    bool originOk = isOriginAllowed(origin);

    HttpResponse hresp;
    if (req.method == "OPTIONS") {
        hresp.status = 204;
        hresp.contentType = "text/plain";
        hresp.body = "";
    } else if (requestHandler_) {
        hresp = requestHandler_(req);
    } else {
        hresp.status = 404;
        hresp.body = "{\"error\":\"Not found\"}";
    }

    std::ostringstream raw;
    raw << "HTTP/1.1 " << hresp.status << " " << statusReason(hresp.status) << "\r\n"
        << "Content-Type: " << hresp.contentType << "\r\n"
        << "Content-Length: " << hresp.body.size() << "\r\n"
        << "X-Request-Id: " << req.requestId << "\r\n"
        << "X-Content-Type-Options: nosniff\r\n"
        << "Cache-Control: no-store\r\n";

    if (originOk) {
        raw << "Access-Control-Allow-Origin: " << origin << "\r\n"
            << "Vary: Origin\r\n"
            << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
            << "Access-Control-Allow-Headers: Content-Type, Authorization, X-Request-Id\r\n";
    }
    for (const auto& [hk, hv] : hresp.extraHeaders) {
        raw << hk << ": " << hv << "\r\n";
    }
    raw << "Connection: close\r\n\r\n" << hresp.body;

    std::string rstr = raw.str();
    sendAll(sock, rstr.data(), rstr.size());

    std::lock_guard<std::mutex> lock(workerMutex_);
    activeSockets_.erase(sock);
    closeSock(sock);
}

void HttpWsServer::handleWebSocket(ApiSocket sock) {
#ifdef _WIN32
    DWORD tv = 60000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv{60, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        wsClients_.insert(sock);
    }

    // Read inbound WebSocket frames (handle ping/pong/close; keep connection open)
    while (running_.load()) {
        uint8_t hdr[2]{};
        int n = ::recv(sock, reinterpret_cast<char*>(hdr), 2, 0);
        if (n <= 0) {
#ifdef _WIN32
            if (n < 0 && WSAGetLastError() == WSAETIMEDOUT) continue;
#else
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
#endif
            break;
        }
        if (n == 1) {
            int n2 = ::recv(sock, reinterpret_cast<char*>(hdr + 1), 1, 0);
            if (n2 <= 0) break;
        }

        uint8_t opcode = hdr[0] & 0x0F;
        bool masked = (hdr[1] & 0x80) != 0;
        uint64_t len = hdr[1] & 0x7F;

        if (len == 126) {
            std::string ext;
            if (!recvExact(sock, ext, 2)) break;
            len = (static_cast<uint64_t>(static_cast<uint8_t>(ext[0])) << 8) |
                   static_cast<uint64_t>(static_cast<uint8_t>(ext[1]));
        } else if (len == 127) {
            std::string ext;
            if (!recvExact(sock, ext, 8)) break;
            len = 0;
            for (int i = 0; i < 8; ++i) {
                len = (len << 8) | static_cast<uint8_t>(ext[i]);
            }
        }
        if (len > 65536ULL) break;

        uint8_t maskKey[4]{};
        if (masked) {
            std::string mk;
            if (!recvExact(sock, mk, 4)) break;
            std::memcpy(maskKey, mk.data(), 4);
        }

        std::string payload;
        if (len > 0) {
            if (!recvExact(sock, payload, static_cast<size_t>(len))) break;
            if (masked) {
                for (size_t i = 0; i < payload.size(); ++i) {
                    payload[i] = static_cast<char>(static_cast<uint8_t>(payload[i]) ^ maskKey[i % 4]);
                }
            }
        }

        if (opcode == 0x8) { // CLOSE
            uint8_t closeFrame[2] = {0x88, 0x00};
            sendAll(sock, reinterpret_cast<const char*>(closeFrame), 2);
            break;
        } else if (opcode == 0x9) { // PING -> PONG
            std::string pong;
            pong.push_back(static_cast<char>(0x8A));
            pong.push_back(static_cast<char>(payload.size() & 0x7F));
            pong.append(payload);
            std::lock_guard<std::mutex> lock(wsMutex_);
            sendAll(sock, pong.data(), pong.size());
        }
    }

    std::lock_guard<std::mutex> lock(wsMutex_);
    wsClients_.erase(sock);
}

std::string HttpWsServer::buildWsTextFrame(const std::string& payload) {
    std::string frame;
    frame.reserve(payload.size() + 10);
    frame.push_back(static_cast<char>(0x81)); // FIN + TEXT
    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back(static_cast<char>(len));
    } else if (len <= 65535) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
        }
    }
    frame.append(payload);
    return frame;
}

void HttpWsServer::broadcastWsText(const std::string& message) {
    std::string frame = buildWsTextFrame(message);
    std::lock_guard<std::mutex> lock(wsMutex_);
    std::vector<ApiSocket> dead;
    for (ApiSocket s : wsClients_) {
        if (!sendAll(s, frame.data(), frame.size())) {
            dead.push_back(s);
        }
    }
    for (ApiSocket s : dead) {
        wsClients_.erase(s);
    }
}

} // namespace dlm
