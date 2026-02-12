#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../src/ast_nodes.hpp"
#include "../src/parser.hpp"
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

ASTNode parse_source(const std::string& source_text) {
    std::vector<Token> tokens = tokenize_source(source_text);
    Parser parser(std::move(tokens), "tests/input.mtc");
    return parser.parse_program();
}

ASTNode parse_file(const std::string& file_path) {
    std::ifstream input(file_path);
    expect(input.is_open(), "failed to open parser input file: " + file_path);

    std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::string parser_file_path = file_path;
    Tokenizer tokenizer(source, parser_file_path);
    std::vector<Token> tokens = tokenizer.tokenize();

    Parser parser(std::move(tokens), parser_file_path);
    return parser.parse_program();
}

void test_parse_fixture_program() {
    ASTNode root = parse_file("tests/test.mtc");
    expect(is_node<Program>(root), "expected root node to be Program");

    const auto& program = get_node<Program>(root);
    expect(program.statements.size() == 2, "expected two top-level statements in test fixture");

    expect(is_node<FromImportStatement>(program.statements[0]), "expected first statement to be import");
    const auto& import_stmt = get_node<FromImportStatement>(program.statements[0]);
    expect(!import_stmt.is_wildcard, "expected named import (not wildcard)");
    expect(import_stmt.symbols.size() == 1 && import_stmt.symbols[0] == "range",
           "expected `range` symbol import");

    expect(is_node<ForInStatement>(program.statements[1]), "expected second statement to be for-in");
    const auto& for_stmt = get_node<ForInStatement>(program.statements[1]);
    expect(for_stmt.variable == "i", "expected loop variable i");
    expect(is_node<CallExpression>(for_stmt.iterable), "expected loop iterable to be function call");

    const auto& body = get_node<Block>(for_stmt.body);
    expect(body.statements.size() == 1, "expected one statement inside loop body");
}

void test_typed_declarations() {
    ASTNode root = parse_source(
        "array<int> nums = [1, 2]\n"
        "dict<string, int> counts = {\"a\": 1}\n");

    const auto& program = get_node<Program>(root);
    expect(program.statements.size() == 2, "expected two declarations");

    expect(is_node<VariableDeclaration>(program.statements[0]), "expected first declaration node");
    const auto& array_decl = get_node<VariableDeclaration>(program.statements[0]);
    expect(array_decl.type == "array", "expected array declaration type");
    expect(array_decl.element_type == "int", "expected array element type int");

    expect(is_node<VariableDeclaration>(program.statements[1]), "expected second declaration node");
    const auto& dict_decl = get_node<VariableDeclaration>(program.statements[1]);
    expect(dict_decl.type == "dict", "expected dict declaration type");
    expect(dict_decl.key_type == "string", "expected dict key type string");
    expect(dict_decl.value_type == "int", "expected dict value type int");
}

void test_fixed_size_array_declaration() {
    ASTNode root = parse_source(
        "int[5] nums = [1, 2, 3]\n"
        "array<int>[8] cache\n");

    const auto& program = get_node<Program>(root);
    expect(program.statements.size() == 2, "expected two fixed-size declarations");

    expect(is_node<VariableDeclaration>(program.statements[0]), "expected first fixed-size declaration");
    const auto& first_decl = get_node<VariableDeclaration>(program.statements[0]);
    expect(first_decl.type == "array", "expected fixed-size declaration to map to array type");
    expect(first_decl.element_type == "int", "expected fixed-size array element type int");
    expect(first_decl.fixed_size == 5, "expected fixed-size value 5");

    expect(is_node<VariableDeclaration>(program.statements[1]), "expected second fixed-size declaration");
    const auto& second_decl = get_node<VariableDeclaration>(program.statements[1]);
    expect(second_decl.type == "array", "expected generic fixed-size declaration to map to array");
    expect(second_decl.element_type == "int", "expected generic fixed-size element type int");
    expect(second_decl.fixed_size == 8, "expected fixed-size value 8");
}

void test_dynamic_array_keyword_declaration() {
    ASTNode root = parse_source("dynamic array ints = [0, 1, 2]\n");

    const auto& program = get_node<Program>(root);
    expect(program.statements.size() == 1, "expected one dynamic declaration");
    expect(is_node<VariableDeclaration>(program.statements[0]),
           "expected dynamic declaration to produce VariableDeclaration");

    const auto& decl = get_node<VariableDeclaration>(program.statements[0]);
    expect(decl.type == "array", "expected dynamic array declaration type");
    expect(decl.fixed_size == -1, "expected dynamic array to avoid fixed stack size");
}

void test_class_declaration() {
    ASTNode root = parse_source(
        "class Counter {\n"
        "  arg int seed\n"
        "  int value = 0\n"
        "  int next(int step) {\n"
        "    return step\n"
        "  }\n"
        "}\n");

    const auto& program = get_node<Program>(root);
    expect(program.statements.size() == 1, "expected one top-level class declaration");
    expect(is_node<ClassDeclaration>(program.statements[0]), "expected class declaration node");

    const auto& class_decl = get_node<ClassDeclaration>(program.statements[0]);
    expect(class_decl.name == "Counter", "expected class name Counter");
    expect(class_decl.fields.size() == 2, "expected two class fields");
    expect(class_decl.fields[0].is_constructor_arg, "expected first field to be constructor arg");
    expect(class_decl.methods.size() == 1, "expected one class method");
}

void test_invalid_libc_wildcard_import() {
    bool threw = false;
    try {
        (void)parse_source("from libc use *");
    } catch (const CompilerError& err) {
        threw = true;
        expect(std::string(err.what()).find("Wildcard import not allowed for libc module") !=
                   std::string::npos,
               "expected libc wildcard import rejection");
    }
    expect(threw, "expected wildcard libc import to throw CompilerError");
}

}  // namespace

int main() {
    test_parse_fixture_program();
    test_typed_declarations();
    test_fixed_size_array_declaration();
    test_dynamic_array_keyword_declaration();
    test_class_declaration();
    test_invalid_libc_wildcard_import();
    std::cout << "All parser tests passed\n";
    return 0;
}
