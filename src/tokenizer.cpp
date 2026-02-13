#include "tokenizer.hpp"
#include <cctype>
#include <iostream>

const std::unordered_set<std::string> KEYWORDS = {"from", "use", "as", "int", "float", "void", "array", "dict", "string", "if", "elif", "else", "for", "in", "set", "return", "typeof", "hasattr", "classof", "while", "bool", "true", "false", "func", "class", "new", "this", "static", "virtual", "null", "try", "catch", "throw", "except", "break", "external", "dynamic"};
const std::unordered_set<std::string> SINGLE_CHAR_SYMBOLS = {"(", ")", "[", "]", "{", "}", ",", ".", ":", "!", "+", "-", "*", "/", "=", ">", "<"};
const std::unordered_set<std::string> DOUBLE_CHAR_SYMBOLS = {"==", "!=", "+=", ">=", "<=", "&&", "||"};
const std::unordered_set<std::string> QUOTES = {"\"", "'"};
const std::unordered_set<std::string> WHITESPACE = {" ", "\t", "\n", "\r"};

Tokenizer::Tokenizer(std::string& s, std::string& fp)
    : source(s), file_path(fp), position(0), line(1), col(1) {}

char Tokenizer::current_char() {
    return source[position];
}

void Tokenizer::advance(int offset) {
    for (int i = 0; i < offset; i++) {
        if (position < (int)source.length()) {
            if (source[position] == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
        }
        position++;
    }
}

bool Tokenizer::is_at_end() {
    return position >= (int)source.length();
}

char Tokenizer::peek_next(int offset) {
    if (position + offset < (int)source.length()) {
        return source[position + offset];
    }
    return '\0';
}

void Tokenizer::skip_whitespace() {
    while (!is_at_end() && WHITESPACE.count(std::string(1, current_char()))) {
        advance();
    }
}

void Tokenizer::skip_comment() {
    if (!is_at_end() && current_char() == '/' && peek_next() == '/') {
        advance();
        advance();
        while (!is_at_end() && current_char() != '\n' && current_char() != '\r') {
            advance();
        }
    }
}

Token Tokenizer::read_word() {
    std::string word = "";
    int start_line = line;
    int start_col = col;
    while (!is_at_end() && (std::isalnum(current_char()) || current_char() == '_')) {
        word += current_char();
        advance();
    }
    if (KEYWORDS.count(word)) {
        return Token(T_KEYWORD, word, start_line, start_col);
    } else {
        return Token(T_NAME, word, start_line, start_col);
    }
}

Token Tokenizer::read_number() {
    std::string number = "";
    bool has_decimal = false;
    bool has_exponent = false;
    int start_line = line;
    int start_col = col;

    while (!is_at_end() && std::isdigit(current_char())) {
        number += current_char();
        advance();
    }

    if (!is_at_end() && current_char() == '.') {
        has_decimal = true;
        number += ".";
        advance();
        if (!is_at_end() && std::isdigit(current_char())) {
            while (!is_at_end() && std::isdigit(current_char())) {
                number += current_char();
                advance();
            }
        } else {
            throw CompilerError(
                std::string("Invalid float literal at line ") + std::to_string(start_line) +
                ": expected digits after decimal point", "ERROR", file_path);
        }
    }

    if (!is_at_end() && std::tolower(current_char()) == 'e') {
        has_exponent = true;
        number += "e";
        advance();
        if (!is_at_end() && std::string("+-").find(current_char()) != std::string::npos) {
            number += current_char();
            advance();
        }
        if (!is_at_end() && std::isdigit(current_char())) {
            while (!is_at_end() && std::isdigit(current_char())) {
                number += current_char();
                advance();
            }
        } else {
            throw CompilerError(
                std::string("Invalid float literal at line ") + std::to_string(start_line) +
                ": expected digits in exponent", "ERROR", file_path);
        }
    }

    bool is_float = has_decimal || has_exponent;
    int token_type = is_float ? T_FLOAT_LITERAL : T_INT_LITERAL;
    return Token(token_type, number, start_line, start_col);
}

Token Tokenizer::read_string() {
    std::string str = "";
    char quote_char = current_char();
    int start_line = line;
    int start_col = col;
    advance();

    while (!is_at_end() && current_char() != quote_char) {
        if (current_char() == '\\' && !is_at_end()) {
            advance();
            if (is_at_end()) break;
            char escape_char = current_char();
            switch (escape_char) {
                case 'n':  str += '\n'; advance(); break;
                case 't':  str += '\t'; advance(); break;
                case 'r':  str += '\r'; advance(); break;
                case '\\': str += '\\'; advance(); break;
                case '"':  str += '"';  advance(); break;
                case '\'': str += '\''; advance(); break;
                default:
                    if (escape_char >= '0' && escape_char <= '7' &&
                        peek_next(1) >= '0' && peek_next(1) <= '7' &&
                        peek_next(2) >= '0' && peek_next(2) <= '7') {
                        std::string octal_str;
                        octal_str += escape_char;
                        octal_str += peek_next(1);
                        octal_str += peek_next(2);
                        int octal_val = std::stoi(octal_str, nullptr, 8);
                        str += static_cast<char>(octal_val);
                        advance(3);
                    } else {
                        str += '\\';
                        str += escape_char;
                        advance();
                    }
                    break;
            }
        } else {
            str += current_char();
            advance();
        }
    }

    if (is_at_end()) {
        throw CompilerError(
            std::string("Unterminated string literal at line ") + std::to_string(start_line) +
            ", column " + std::to_string(start_col), "ERROR", file_path);
    }
    advance();
    return Token(T_STRING, str, start_line, start_col);
}

Token Tokenizer::read_symbol() {
    if (!is_at_end()) {
        std::string sym;
        sym += current_char();
        if (peek_next() != '\0') {
            sym += peek_next();
        }
        if (peek_next() != '\0' && DOUBLE_CHAR_SYMBOLS.count(sym)) {
            Token token(T_SYMBOL, sym, line, col);
            advance(2);
            return token;
        } else if (SINGLE_CHAR_SYMBOLS.count(std::string(1, current_char()))) {
            Token token(T_SYMBOL, std::string(1, current_char()), line, col);
            advance();
            return token;
        } else {
            throw CompilerError(
                std::string("Unknown symbol '") + std::string(1, current_char()) +
                "' at line " + std::to_string(line) + ", column " + std::to_string(col),
                "ERROR", file_path);
        }
    }
    throw CompilerError("Unexpected end of input while reading symbol", "ERROR", file_path);
}

std::vector<Token> Tokenizer::tokenize() {
    std::vector<Token> tokens;
    while (!is_at_end()) {
        while (!is_at_end()) {
            std::size_t start_pos = position;
            skip_whitespace();
            skip_comment();
            if (position == static_cast<int>(start_pos)) {
                break;
            }
        }
        if (is_at_end()) break;

        if (QUOTES.count(std::string(1, current_char()))) {
            tokens.push_back(read_string());
        } else if (current_char() == '-') {
            char next_char = !is_at_end() ? peek_next() : '\0';
            if (next_char != '\0' && std::isdigit(next_char)) {
                advance();
                std::string number = "-";
                int start_line = line;
                int start_col = col;
                Token num_token = read_number();
                number += num_token.value;
                int token_type = num_token.type == T_FLOAT_LITERAL ? T_FLOAT_LITERAL : T_INT_LITERAL;
                tokens.push_back(Token(token_type, number, start_line, start_col));
            } else {
                tokens.push_back(read_symbol());
            }
        } else if (SINGLE_CHAR_SYMBOLS.count(std::string(1, current_char())) ||
                   ([&]() {
                       if (peek_next() == '\0') return false;
                       std::string sym;
                       sym += current_char();
                       sym += peek_next();
                       return DOUBLE_CHAR_SYMBOLS.count(sym) > 0;
                   })()) {
            tokens.push_back(read_symbol());
        } else if (std::isalpha(current_char()) || current_char() == '_') {
            tokens.push_back(read_word());
        } else if (std::isdigit(current_char())) {
            tokens.push_back(read_number());
        } else {
            std::string c = !is_at_end() ? std::string(1, current_char()) : "EOF";
            throw CompilerError(
                std::string("Tokenizer failed at char ") + std::to_string(position) +
                ": '" + c + "' (line " + std::to_string(line) +
                ", column " + std::to_string(col) + ")",
                "ERROR", file_path);
        }
    }
    return tokens;
}

std::string token_type_name(int token_type) {
    switch (token_type) {
        case T_NAME: return "NAME";
        case T_KEYWORD: return "KEYWORD";
        case T_FLOAT_LITERAL: return "FLOAT_LITERAL";
        case T_INT_LITERAL: return "INT_LITERAL";
        case T_SYMBOL: return "SYMBOL";
        case T_STRING: return "STRING";
        default: return "UNKNOWN";
    }
}

std::string escape_token_value(const std::string& value) {
    std::string escaped;
    for (char ch : value) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '\"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\t': escaped += "\\t"; break;
            case '\r': escaped += "\\r"; break;
            default: escaped += ch; break;
        }
    }
    return escaped;
}

std::string format_tokens(const std::vector<Token>& tokens) {
    std::string out = "[";
    for (size_t i = 0; i < tokens.size(); i++) {
        if (i > 0) out += ", ";
        out += "<\"" + token_type_name(tokens[i].type) + "\", \"" + escape_token_value(tokens[i].value) + "\">";
    }
    out += "]";
    return out;
}
