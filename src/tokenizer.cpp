#include <iostream>
#include <string>
#include <unordered_set>
#include <exception>
#include <cctype>
#include <vector>


const std::unordered_set<std::string> KEYWORDS = {"from", "use", "as", "int", "float", "void", "array", "dict", "string", "if", "elif", "else", "for", "in", "set", "return", "typeof", "hasattr", "classof", "while", "bool", "true", "false", "func", "class", "new", "this", "static", "virtual", "arg", "null", "try", "catch", "throw", "except", "break", "external"};
const std::unordered_set<std::string> SINGLE_CHAR_SYMBOLS = {"(", ")", "[", "]", "{", "}", ",", ".", ":", "!", "+", "-", "*", "/", "=", ">", "<"};
const std::unordered_set<std::string> DOUBLE_CHAR_SYMBOLS = {"==", "!=", "+=", ">=", "<=", "&&", "||"};
const std::unordered_set<std::string> QUOTES = {"\"", "'"};
const std::unordered_set<std::string> WHITESPACE = {" ", "\t", "\n"};

#define T_NAME          0
#define T_KEYWORD       1
#define T_FLOAT_LITERAL 2
#define T_INT_LITERAL   3
#define T_SYMBOL        4
#define T_STRING        5

class Token {
public:
    int type;
    std::string value;
    int line;
    int column;

    // constructor with default params for line and col
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

    // override what() to return the error message
    const char* what() const noexcept override {
        return msg.c_str();
    }

    const std::string& getSeverity() const {
        return severity;
    }
};

class Tokenizer{
private:
    std::string source;
    std::string file_path;
    int position;
    int line;
    int col;

public:
    Tokenizer(std::string& s, std::string& fp)
        : source(s), file_path(fp), position(0), line(1), col(1) {}

    char current_char(void){
        return source[position];
    }
    void advance(int offset=1){
        for (int i = 0; i < offset; i++){
            if (position < (int)source.length()){
                if (source[position] == '\n'){
                    line++;
                    col = 1;
                }
                else{
                    col++;
                }
            }
            position++;
        }
    }
    bool is_at_end(void){
        if (position >= (int)source.length()){
            return true;
        }
        return false;
    }
    char peek_next(int offset=1){
        if (position + offset < (int)source.length()){
            return source[position+offset];
        }
        return '\0';
    }
    void skip_whitespace(void){
        while (!is_at_end() && WHITESPACE.count(std::string(1, current_char()))){ // skipping whitespace
            advance();
        }
    }
    void skip_comment(void){
        if (!is_at_end() && current_char() == '/' && peek_next() == '/'){ // comment detected
            advance(); // skip first / char
            advance(); // skip second / char
            while (!is_at_end() && current_char() != '\n'){
                advance();
            }
        }
    }
    Token read_word(void){
        std::string word = "";
        int start_line = line;
        int start_col = col;
        while (!is_at_end() && (std::isalnum(current_char()) || current_char() == '_')){
            word += current_char();
            advance();
        }
        if (KEYWORDS.count(word)){
            return Token(T_KEYWORD, word, start_line, start_col);
        }
        else{
            return Token(T_NAME, word, start_line, start_col);
        }
    }
    Token read_number(void){
        std::string number = "";
        bool has_decimal = false;
        bool has_exponent = false;
        int start_line = line;
        int start_col = col;
        // read digits before . char
        while (!is_at_end() && std::isdigit(current_char())){
            number += current_char();
            advance();
        }
        // handle . char
        if (!is_at_end() && current_char() == '.'){
            has_decimal = true;
            number += ".";
            advance();
            // read digits after decimal
            if (!is_at_end() && std::isdigit(current_char())){
                while (!is_at_end() && std::isdigit(current_char())){
                    number += current_char();
                    advance();
                }
            }
            else{
                throw CompilerError(std::string("Invalid float literal at line ") + std::to_string(start_line) + ": expected digits after decimal point", "ERROR", file_path);
            }
        }
        if (!is_at_end() && std::tolower(current_char()) == 'e'){
            has_exponent = true;
            number += "e";
            advance();

            if (!is_at_end() && std::string("+-").find(current_char()) != std::string::npos){
                number += current_char();
                advance();
            }
            if (!is_at_end() && std::isdigit(current_char())){
                while (!is_at_end() && std::isdigit(current_char())){
                    number += current_char();
                    advance();
                }
            }
            else{
                throw CompilerError(std::string("Invalid float literal at line ") + std::to_string(start_line) + ": expected digits in exponent", "ERROR", file_path);
            }
        }
        bool is_float = has_decimal || has_exponent;
        int token_type = is_float ? T_FLOAT_LITERAL : T_INT_LITERAL;
        return Token(token_type, number, start_line, start_col);

    }
    Token read_string(void){
        std::string str = "";
        char quote_char = current_char();
        int start_line = line;
        int start_col = col;
        advance(); // consume the quote
        std::string octal_ints = "01234567";
        while (!is_at_end() && current_char() != quote_char){
            if (current_char() == '\\' && !is_at_end()){
                // handle escape sequences
                advance();
                if (is_at_end()){
                    break;
                }
                char escape_char = current_char();
                switch (escape_char) {
                    case 'n':  str += '\n'; advance(); break;
                    case 't':  str += '\t'; advance(); break;
                    case 'r':  str += '\r'; advance(); break;
                    case '\\': str += '\\'; advance(); break;
                    case '"':  str += '"'; advance(); break;
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
                            advance(3); // consume all three octal digits

                        } else {
                            str += '\\';
                            str += escape_char;
                            advance();
                        }
                        break;
                }
            }
            else{
                str+= current_char();
                advance();
            }
        }
        if (is_at_end()){
            throw CompilerError(std::string("Unterminated string literal at line ") + std::to_string(start_line) + ", column " + std::to_string(start_col), "ERROR", file_path);
        }
        advance();
        return Token(T_STRING, str, start_line, start_col);
    }
    Token read_symbol(void){
        skip_comment();
        if (!is_at_end()){
            std::string sym;
            sym += current_char();
            if (peek_next() != '\0') {
                sym += peek_next();
            }
            if (peek_next() != '\0' && DOUBLE_CHAR_SYMBOLS.count(sym)){
                Token token(T_SYMBOL, sym, line, col);
                advance(2);
                return token;
            }
            else if (SINGLE_CHAR_SYMBOLS.count(std::string(1, current_char()))){
                Token token(T_SYMBOL, std::string(1, current_char()), line, col);
                advance();
                return token;
            }
            else{
                throw CompilerError(std::string("Unknown symbol '") + std::to_string(current_char()) + "' at line " + std::to_string(line) + ", column " + std::to_string(col), "ERROR", file_path);
            }
            
        }
        throw CompilerError("Unexpected end of input while reading symbol", "ERROR", file_path);
    }
    std::vector<Token> tokenize(void){
        std::vector<Token> tokens;
        while (!is_at_end()){
            skip_whitespace();
            skip_comment();
            skip_whitespace();
            if (is_at_end()){
                break;
            }
            if (QUOTES.count(std::string(1, current_char()))){
                tokens.push_back(read_string());
            }
            else if (current_char() == '-'){
                // check if its a negative number

                char next_char = !is_at_end() ? peek_next() : '\0';
                if (next_char != '\0' && std::isdigit(next_char)){
                    advance();
                    std::string number = "-";
                    int start_line = line;
                    int start_col = col;

                    Token num_token = read_number();
                    number += num_token.value;

                    int token_type = num_token.type == T_FLOAT_LITERAL ? T_FLOAT_LITERAL : T_INT_LITERAL;
                    tokens.push_back(Token(token_type, number, start_line, start_col));
                }
                else{ // its a binary operation
                    tokens.push_back(read_symbol());
                }
            }
            else if (SINGLE_CHAR_SYMBOLS.count(std::string(1, current_char())) ||
                     ([&]() {
                         if (peek_next() == '\0') {
                             return false;
                         }
                         std::string sym;
                         sym += current_char();
                         sym += peek_next();
                         return DOUBLE_CHAR_SYMBOLS.count(sym) > 0;
                     })() ||
                     (current_char() == '/' && peek_next() == '/')){
                tokens.push_back(read_symbol());
            }
            else if (std::isalpha(current_char()) || current_char() == '_'){
                tokens.push_back(read_word());
            }
            else if (std::isdigit(current_char())){
                tokens.push_back(read_number());
            }
            else{
                std::string c = !is_at_end() ? std::string(1, current_char()) : "EOF";
                throw CompilerError(
                    std::string("Tokenizer failed at char ") + std::to_string(position) +
                    ": '" + c + "' (line " + std::to_string(line) +
                    ", column " + std::to_string(col) + ")",
                    "ERROR",
                    file_path
                );
            }
        }
        return tokens;
    }

};


int main(){
    std::cout << "Hello world\n";
    
}
