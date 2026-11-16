#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "dlm/common/Types.h"

namespace dlm {

enum class SqlCommandType : uint8_t {
    BEGIN,
    PUT,
    GET,
    DEL,
    EXISTS,
    SCAN,
    COMMIT,
    ABORT,
    SHOW_LOCKS,
    SHOW_TRANSACTIONS,
    SHOW_NODES
};

struct ParsedCommand {
    SqlCommandType type = SqlCommandType::GET;
    std::string    key;
    std::string    value;
    IsolationLevel isolation = IsolationLevel::SERIALIZABLE;
    size_t         line = 1;
    size_t         column = 1;
};

struct ParseResult {
    bool ok = false;
    std::vector<ParsedCommand> commands;
    std::string error;
    size_t errorLine = 0;
    size_t errorColumn = 0;
};

/// Tokenizer & parser for the DLM SQL-like command language.
/// Supported grammar:
///   script      := (statement ';')*
///   statement   := BEGIN [SERIALIZABLE | READ COMMITTED]
///                | PUT <key> <json_or_string_literal>
///                | GET <key>
///                | DELETE <key>
///                | EXISTS <key>
///                | SCAN [<prefix>]
///                | COMMIT
///                | ABORT | ROLLBACK
///                | SHOW (LOCKS | TRANSACTIONS | NODES)
class CommandParser {
public:
    static ParseResult parse(const std::string& input);
};

} // namespace dlm
