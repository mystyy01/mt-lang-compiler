#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <exception>

// Token type constants
#define T_NAME          0
#define T_KEYWORD       1
#define T_FLOAT_LITERAL 2
#define T_INT_LITERAL   3
#define T_SYMBOL        4
#define T_STRING        5

extern const std::unordered_set<std::string> KEYWORDS;
extern const std::unordered_set<std::string> SINGLE_CHAR_SYMBOLS;
extern const std::unordered_set<std::string> DOUBLE_CHAR_SYMBOLS;
extern const std::unordered_set<std::string> QUOTES;
extern const std::unordered_set<std::string> WHITESPACE;

class Token {
public:
    int type;
    std::string value;
    int line;
    int column;

    Token(int t, const std::string& v, int l = -1, int c = -1)
        : type(t), value(v), line(l), column(c) {}
};

class CompilerError : public std::exception {
private:
    std::string msg;
    std::string severity;

public:
    CompilerError(const std::string& message,
                  const std::string& sev,
                  const std::string& file_path = "")
        : severity(sev)
    {
        std::string file_info;
        if (!file_path.empty() && file_path != "unknown") {
            file_info = " in " + file_path;
        }
        msg = message + file_info + " (Severity: " + severity + ")";
    }

    const char* what() const noexcept override {
        return msg.c_str();
    }

    const std::string& getSeverity() const {
        return severity;
    }
};

class Tokenizer {
private:
    std::string source;
    std::string file_path;
    int position;
    int line;
    int col;

public:
    Tokenizer(std::string& s, std::string& fp);

    char current_char();
    void advance(int offset = 1);
    bool is_at_end();
    char peek_next(int offset = 1);
    void skip_whitespace();
    void skip_comment();
    Token read_word();
    Token read_number();
    Token read_string();
    Token read_symbol();
    std::vector<Token> tokenize();
};

std::string token_type_name(int token_type);
std::string escape_token_value(const std::string& value);
std::string format_tokens(const std::vector<Token>& tokens);
