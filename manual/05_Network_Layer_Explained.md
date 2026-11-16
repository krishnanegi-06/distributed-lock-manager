# Chapter 5: Network & Inter-Process Communication (IPC)

The **Network Layer** enables multiple distributed database nodes running across separate machines or processes to communicate reliably over standard TCP/IP sockets.

---

## 1. Network Message Architecture

All communication in the DLM is message-driven. The structure is declared in `include/dlm/network/Message.h`:

```cpp
enum class MessageType {
    HEARTBEAT,
    LOCK_REQUEST,
    LOCK_GRANT,
    LOCK_DENY,
    LOCK_RELEASE,
    PREPARE,
    VOTE_YES,
    VOTE_NO,
    COMMIT,
    ABORT,
    WFG_REQUEST,
    WFG_RESPONSE
};

struct Message {
    MessageType type;
    NodeId      senderId;
    NodeId      receiverId;
    TxnId       txnId;
    ResourceId  resourceId;
    LockMode    lockMode;
    std::string payload;

    std::string serialize() const;
    static Message deserialize(const std::string& data);
};
```

### Framing & Wire Protocol
In stream-oriented protocols like TCP, messages can arrive in fragments or grouped together. To prevent message boundary corruption, the DLM uses a **4-byte length-prefix framing protocol**:

```
+------------------------+------------------------------------------+
|  Length (4 Bytes, BE)  |           Payload (ASCII Pipe)           |
+------------------------+------------------------------------------+
|      0x0000002A        | 1|101|102|1001|account:10|EXCLUSIVE|DATA |
+------------------------+------------------------------------------+
```

### Serialization Format
`Message::serialize()` packages the message into a pipe-delimited string:
`TYPE|SENDER_ID|RECEIVER_ID|TXN_ID|RESOURCE_ID|LOCK_MODE|PAYLOAD`

For example:
`1|1|2|5001|table:users:row:42|1|OPTIONAL_PAYLOAD`

`Message::deserialize()` splits on `|` delimiters and constructs a typed `Message` struct.

---

## 2. TCP Socket Programming in C++ (`NetworkManager`)

`NetworkManager` provides an asynchronous, multi-threaded TCP server and client.

### Cross-Platform Socket Initialization
On Windows, network sockets require initialization via the Windows Sockets API (Winsock2):
```cpp
void NetworkManager::platformInit() {
#ifdef _WIN32
    WSADATA wsaData;
    int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (res != 0) {
        Logger::error("WSAStartup failed: " + std::to_string(res));
    }
#endif
}
```

### Listening & Accepting Connections (`acceptLoop`)
When `NetworkManager::start(port)` is called:
1. Creates a socket: `SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);`
2. Binds to port: `::bind(sock, (sockaddr*)&addr, sizeof(addr));`
3. Listens: `listen(sock, SOMAXCONN);`
4. Spawns an `acceptLoop` thread that blocks on `accept()`:
   ```cpp
   while (running_) {
       SOCKET clientSock = accept(listenSocket_, nullptr, nullptr);
       if (clientSock != INVALID_SOCKET) {
           // Spawn a worker thread to service this client connection
           std::thread(&NetworkManager::handleClient, this, clientSock).detach();
       }
   }
   ```

### Reading Length-Prefixed Packets (`handleClient`)
The worker thread reads exactly 4 bytes to discover the payload length $N$, then loops on `recv()` until all $N$ bytes of payload are received:

```cpp
uint32_t netLen = 0;
recv(sock, (char*)&netLen, sizeof(netLen), MSG_WAITALL);
uint32_t len = ntohl(netLen);

std::vector<char> buffer(len);
recv(sock, buffer.data(), len, MSG_WAITALL);

Message msg = Message::deserialize(std::string(buffer.begin(), buffer.end()));
dispatchMessage(msg);
```

### Dispatching to Registered Callbacks
Subsystems register callbacks for message types they care about:
```cpp
networkMgr.registerHandler(MessageType::PREPARE, [&](const Message& msg) {
    participant.handlePrepare(msg);
});
```

---

## 3. Node Failure Detection (`HeartbeatManager`)

In a distributed environment, nodes can crash, freeze, or suffer network partitions. Without failure detection, locks held by crashed nodes would remain locked forever, causing permanent cluster hangs.

### How Heartbeat Works:
1. **Periodic Pulse**:
   Every $T_{\text{interval}}$ (e.g. 500ms), `HeartbeatManager` sends a `HEARTBEAT` message to all known peer nodes.
2. **Liveness Tracking**:
   Upon receiving a heartbeat from Node $K$, the receiver updates its heartbeat timestamp:
   ```cpp
   lastHeartbeat_[nodeId] = std::chrono::steady_clock::now();
   ```
3. **Timeout Evaluation**:
   A background monitoring thread periodically checks:
   ```cpp
   auto now = std::chrono::steady_clock::now();
   if (now - lastHeartbeat_[nodeId] > timeoutThreshold_) {
       // Node is declared DEAD!
       markNodeDead(nodeId);
       onNodeFailureCallback_(nodeId);
   }
   ```
4. **Automatic Lock Cleanup**:
   When a node is declared dead, the cluster aborts its in-flight transactions and calls `lockManager.releaseAllLocks(txnId)`, freeing resources for healthy nodes.
