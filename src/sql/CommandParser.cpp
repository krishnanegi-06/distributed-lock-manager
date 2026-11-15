#include "dlm/sql/CommandParser.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>

namespace dlm {

namespace {

enum class TokKind {
    WORD,
    STRING,
    JSON_LITERAL,
    SEMICOLON,
    END_OF_INPUT
};

struct Token {
    TokKind     kind = TokKind::END_OF_INPUT;
    std::string text;
    size_t      line = 1;
    size_t      col  = 1;
};

std::string toUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool tokenize(const std::string& input, std::vector<Token>& out,
              std::string& err, size_t& errLine, size_t& errCol) {
    size_t i = 0;
    size_t line = 1;
    size_t col = 1;
    const size_t n = input.size();

    auto advance = [&](size_t count = 1) {
        for (size_t k = 0; k < count && i < n; ++k) {
            if (input[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
            ++i;
        }
    };

    while (i < n) {
        char c = input[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }
        // Line comments: -- or //
        if ((c == '-' && i + 1 < n && input[i + 1] == '-') ||
            (c == '/' && i + 1 < n && input[i + 1] == '/')) {
            while (i < n && input[i] != '\n') advance();
            continue;
        }
        if (c == ';') {
            out.push_back({TokKind::SEMICOLON, ";", line, col});
            advance();
            continue;
        }
        if (c == '"' || c == '\'') {
            char quote = c;
            size_t tokLine = line, tokCol = col;
            advance(); // skip opening quote
            std::string val;
            bool closed = false;
            while (i < n) {
                char ch = input[i];
                if (ch == '\\' && i + 1 < n) {
                    advance();
                    char esc = input[i];
                    switch (esc) {
                        case 'n': val.push_back('\n'); break;
                        case 'r': val.push_back('\r'); break;
                        case 't': val.push_back('\t'); break;
                        case '\\': val.push_back('\\'); break;
                        case '"': val.push_back('"'); break;
                        case '\'': val.push_back('\''); break;
                        default: val.push_back(esc); break;
                    }
                    advance();
                } else if (ch == quote) {
                    advance();
                    closed = true;
                    break;
                } else {
                    val.push_back(ch);
                    advance();
                }
            }
            if (!closed) {
                err = "Unterminated string literal";
                errLine = tokLine;
                errCol = tokCol;
                return false;
            }
            out.push_back({TokKind::STRING, val, tokLine, tokCol});
            continue;
        }
        if (c == '{' || c == '[') {
            size_t tokLine = line, tokCol = col;
            size_t startIdx = i;
            char openCh = c;
            char closeCh = (openCh == '{') ? '}' : ']';
            int depth = 0;
            bool inStr = false;
            while (i < n) {
                char ch = input[i];
                if (inStr) {
                    if (ch == '\\' && i + 1 < n) {
                        advance(2);
                        continue;
                    }
                    if (ch == '"') inStr = false;
                    advance();
                } else {
                    if (ch == '"') {
                        inStr = true;
                    } else if (ch == openCh) {
                        ++depth;
                    } else if (ch == closeCh) {
                        --depth;
                        if (depth == 0) {
                            advance();
                            break;
                        }
                    }
                    advance();
                }
            }
            if (depth != 0) {
                err = "Unterminated JSON literal";
                errLine = tokLine;
                errCol = tokCol;
                return false;
            }
            std::string jsonTxt = input.substr(startIdx, i - startIdx);
            if (!nlohmann::json::accept(jsonTxt)) {
                err = "Invalid JSON syntax in value literal";
                errLine = tokLine;
                errCol = tokCol;
                return false;
            }
            out.push_back({TokKind::JSON_LITERAL, jsonTxt, tokLine, tokCol});
            continue;
        }

        // Bare word / identifier / number
        size_t tokLine = line, tokCol = col;
        size_t startIdx = i;
        while (i < n) {
            char ch = input[i];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ';') break;
            advance();
        }
        out.push_back({TokKind::WORD, input.substr(startIdx, i - startIdx), tokLine, tokCol});
    }

    out.push_back({TokKind::END_OF_INPUT, "", line, col});
    return true;
}

} // namespace

ParseResult CommandParser::parse(const std::string& input) {
    ParseResult res;
    std::vector<Token> tokens;
    if (!tokenize(input, tokens, res.error, res.errorLine, res.errorColumn)) {
        res.ok = false;
        return res;
    }

    size_t idx = 0;
    auto peek = [&]() -> const Token& { return tokens[idx]; };
    auto consume = [&]() -> const Token& { return tokens[idx++]; };

    auto fail = [&](const std::string& msg, const Token& tok) -> ParseResult {
        res.ok = false;
        res.error = msg;
        res.errorLine = tok.line;
        res.errorColumn = tok.col;
        return res;
    };

    while (peek().kind != TokKind::END_OF_INPUT) {
        while (peek().kind == TokKind::SEMICOLON) consume();
        if (peek().kind == TokKind::END_OF_INPUT) break;

        const Token& cmdTok = consume();
        if (cmdTok.kind != TokKind::WORD) {
            return fail("Expected command keyword, got '" + cmdTok.text + "'", cmdTok);
        }

        std::string kw = toUpper(cmdTok.text);
        ParsedCommand cmd;
        cmd.line = cmdTok.line;
        cmd.column = cmdTok.col;

        if (kw == "BEGIN") {
            cmd.type = SqlCommandType::BEGIN;
            if (peek().kind == TokKind::WORD) {
                std::string iso = toUpper(consume().text);
                if (iso == "SERIALIZABLE") {
                    cmd.isolation = IsolationLevel::SERIALIZABLE;
                } else if (iso == "READ") {
                    if (peek().kind != TokKind::WORD || toUpper(peek().text) != "COMMITTED") {
                        return fail("Expected COMMITTED after READ in BEGIN", peek());
                    }
                    consume();
                    cmd.isolation = IsolationLevel::READ_COMMITTED;
                } else {
                    return fail("Unexpected isolation level '" + iso + "'", cmdTok);
                }
            }
        } else if (kw == "COMMIT") {
            cmd.type = SqlCommandType::COMMIT;
        } else if (kw == "ABORT" || kw == "ROLLBACK") {
            cmd.type = SqlCommandType::ABORT;
        } else if (kw == "GET" || kw == "DELETE" || kw == "EXISTS") {
            if (kw == "GET") cmd.type = SqlCommandType::GET;
            else if (kw == "DELETE") cmd.type = SqlCommandType::DEL;
            else cmd.type = SqlCommandType::EXISTS;

            if (peek().kind != TokKind::WORD && peek().kind != TokKind::STRING) {
                return fail("Expected key after " + kw, peek());
            }
            cmd.key = consume().text;
        } else if (kw == "PUT") {
            cmd.type = SqlCommandType::PUT;
            if (peek().kind != TokKind::WORD && peek().kind != TokKind::STRING) {
                return fail("Expected key after PUT", peek());
            }
            cmd.key = consume().text;

            if (peek().kind == TokKind::SEMICOLON || peek().kind == TokKind::END_OF_INPUT) {
                return fail("Expected value after PUT <key>", peek());
            }
            const Token& valTok = consume();
            cmd.value = valTok.text;
        } else if (kw == "SCAN") {
            cmd.type = SqlCommandType::SCAN;
            if (peek().kind == TokKind::WORD || peek().kind == TokKind::STRING) {
                cmd.key = consume().text;
            }
        } else if (kw == "SHOW") {
            if (peek().kind != TokKind::WORD) {
                return fail("Expected LOCKS, TRANSACTIONS, or NODES after SHOW", peek());
            }
            std::string target = toUpper(consume().text);
            if (target == "LOCKS") cmd.type = SqlCommandType::SHOW_LOCKS;
            else if (target == "TRANSACTIONS") cmd.type = SqlCommandType::SHOW_TRANSACTIONS;
            else if (target == "NODES") cmd.type = SqlCommandType::SHOW_NODES;
            else return fail("Unknown SHOW target '" + target + "'", cmdTok);
        } else {
            return fail("Unknown command '" + cmdTok.text + "'", cmdTok);
        }

        if (peek().kind == TokKind::SEMICOLON) {
            consume();
        } else if (peek().kind != TokKind::END_OF_INPUT) {
            return fail("Expected ';' after statement, got '" + peek().text + "'", peek());
        }

        res.commands.push_back(std::move(cmd));
    }

    if (res.commands.empty()) {
        res.ok = false;
        res.error = "No statements provided";
        res.errorLine = 1;
        res.errorColumn = 1;
        return res;
    }

    res.ok = true;
    return res;
}

} // namespace dlm
