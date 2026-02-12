#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../src/tokenizer.hpp"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

std::vector<Token> tokenize_source(const std::string& source_text) {
    std::string source = source_text;
    std::string file_path = "tests/input.mtc";
    Tokenizer tokenizer(source, file_path);
    return tokenizer.tokenize();
}

std::vector<Token> tokenize_file(const std::string& file_path) {
    std::ifstream input(file_path);
    expect(input.is_open(), "failed to open tokenizer input file: " + file_path);
    std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::string tokenizer_file_path = file_path;
    Tokenizer tokenizer(source, tokenizer_file_path);
    return tokenizer.tokenize();
}

void test_keyword_identifier_integer() {
    const auto tokens = tokenize_source("int value = 42");
    expect(tokens.size() == 4, "expected 4 tokens for simple declaration");
    expect(tokens[0].type == T_KEYWORD && tokens[0].value == "int", "expected keyword token 'int'");
    expect(tokens[1].type == T_NAME && tokens[1].value == "value", "expected identifier token 'value'");
    expect(tokens[2].type == T_SYMBOL && tokens[2].value == "=", "expected symbol token '='");
    expect(tokens[3].type == T_INT_LITERAL && tokens[3].value == "42", "expected integer literal token '42'");
}

void test_negative_float_with_exponent() {
    const auto tokens = tokenize_source("x = -1.5e+2");
    expect(tokens.size() == 3, "expected 3 tokens for assignment with negative float");
    expect(tokens[0].type == T_NAME && tokens[0].value == "x", "expected identifier token 'x'");
    expect(tokens[1].type == T_SYMBOL && tokens[1].value == "=", "expected symbol token '='");
    expect(tokens[2].type == T_FLOAT_LITERAL && tokens[2].value == "-1.5e+2", "expected float literal '-1.5e+2'");
}

void test_string_escapes_and_comment_skip() {
    const auto tokens = tokenize_source("name = \"a\\n\\101\"\n// this is a comment\nreturn name");
    expect(tokens.size() == 5, "expected 5 tokens with comment skipped");
    expect(tokens[0].type == T_NAME && tokens[0].value == "name", "expected identifier token 'name'");
    expect(tokens[1].type == T_SYMBOL && tokens[1].value == "=", "expected symbol token '='");
    expect(tokens[2].type == T_STRING && tokens[2].value == "a\nA", "expected escaped string token value");
    expect(tokens[3].type == T_KEYWORD && tokens[3].value == "return", "expected keyword token 'return'");
    expect(tokens[4].type == T_NAME && tokens[4].value == "name", "expected identifier token 'name'");
}

void test_invalid_float_throws() {
    bool threw = false;
    try {
        (void)tokenize_source("value = 1.");
    } catch (const CompilerError& err) {
        threw = true;
        expect(std::string(err.what()).find("Invalid float literal") != std::string::npos,
               "expected invalid float literal error");
    }
    expect(threw, "expected invalid float input to throw CompilerError");
}

void test_unterminated_string_throws() {
    bool threw = false;
    try {
        (void)tokenize_source("\"unterminated");
    } catch (const CompilerError& err) {
        threw = true;
        expect(std::string(err.what()).find("Unterminated string literal") != std::string::npos,
               "expected unterminated string literal error");
    }
    expect(threw, "expected unterminated string to throw CompilerError");
}

void test_formatted_output() {
    const auto tokens = tokenize_file("tests/test.mtc");
    const std::string formatted = format_tokens(tokens);
    std::cout << formatted << "\n";
    expect(formatted == "[<\"KEYWORD\", \"from\">, <\"NAME\", \"stdlib\">, <\"SYMBOL\", \".\">, <\"NAME\", \"iter\">, <\"KEYWORD\", \"use\">, <\"NAME\", \"range\">, <\"KEYWORD\", \"for\">, <\"SYMBOL\", \"(\">, <\"NAME\", \"i\">, <\"KEYWORD\", \"in\">, <\"NAME\", \"range\">, <\"SYMBOL\", \"(\">, <\"INT_LITERAL\", \"5\">, <\"SYMBOL\", \")\">, <\"SYMBOL\", \")\">, <\"SYMBOL\", \"{\">, <\"NAME\", \"print\">, <\"SYMBOL\", \"(\">, <\"NAME\", \"i\">, <\"SYMBOL\", \")\">, <\"SYMBOL\", \"}\">]",
           "expected exact token output format");
}

}  // namespace

int main() {
    test_keyword_identifier_integer();
    test_negative_float_with_exponent();
    test_string_escapes_and_comment_skip();
    test_invalid_float_throws();
    test_unterminated_string_throws();
    test_formatted_output();
    std::cout << "All tokenizer tests passed\n";
    return 0;
}
