#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../src/parser.hpp"
#include "../src/semantic.hpp"
#include "../src/tokenizer.hpp"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

ASTNode parse_source(const std::string& source_text) {
    std::string source = source_text;
    std::string file_path = "tests/semantic_input.mtc";
    Tokenizer tokenizer(source, file_path);
    std::vector<Token> tokens = tokenizer.tokenize();
    Parser parser(std::move(tokens), file_path);
    return parser.parse_program();
}

std::vector<std::string> analyze_source(const std::string& source_text) {
    ASTNode root = parse_source(source_text);
    SemanticAnalyzer analyzer("tests/semantic_input.mtc");
    analyzer.analyze(root);
    return analyzer.get_errors();
}

bool contains_error(const std::vector<std::string>& errors, const std::string& fragment) {
    for (const auto& error : errors) {
        if (error.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void test_valid_semantics() {
    const auto errors = analyze_source(
        "int add(int a, int b = 2) { return a + b }\n"
        "int x = add(5)\n"
        "class Counter {\n"
        "  int value = 0\n"
        "  int next(int step) { return step }\n"
        "}\n"
        "Counter c = new Counter()\n"
        "int n = c.next(1)\n");

    expect(errors.empty(), "expected no semantic errors for valid program");
}

void test_assignment_type_mismatch() {
    const auto errors = analyze_source("int x = \"oops\"\n");
    expect(contains_error(errors, "Cannot assign type string to int"),
           "expected string-to-int assignment error");
}

void test_unknown_function_call() {
    const auto errors = analyze_source("int x = missing(1)\n");
    expect(contains_error(errors, "Unknown function 'missing'"),
           "expected unknown function semantic error");
}

void test_method_argument_type_mismatch() {
    const auto errors = analyze_source(
        "class Counter { int next(int step) { return step } }\n"
        "Counter c = new Counter()\n"
        "int x = c.next(\"bad\")\n");

    expect(contains_error(errors, "Argument 1 of method 'next' expects int, got string"),
           "expected method argument type mismatch");
}

void test_fixed_array_initializer_bounds() {
    const auto overflow_errors = analyze_source("int[2] nums = [1, 2, 3]\n");
    expect(contains_error(overflow_errors, "Array initializer has too many elements for fixed size 2"),
           "expected fixed-size initializer overflow error");

    const auto valid_errors = analyze_source(
        "int[4] nums = [1, 2]\n"
        "set nums[3] = 7\n");
    expect(valid_errors.empty(), "expected valid fixed-size array usage to pass semantic analysis");
}

void test_dynamic_array_keyword_semantics() {
    const auto errors = analyze_source(
        "dynamic array nums = [0, 1, 2]\n"
        "set nums[1] = 5\n"
        "int x = nums[1]\n");

    expect(errors.empty(), "expected dynamic array keyword program to pass semantic analysis");
}

void test_dynamic_array_methods_semantics() {
    const auto errors = analyze_source(
        "dynamic array nums = [0, 1, 2, 3]\n"
        "nums.append(4)\n"
        "int a = nums.pop()\n"
        "int b = nums.length()\n"
        "int c = length(nums)\n");

    expect(errors.empty(), "expected dynamic array methods to pass semantic analysis");
}

void test_this_field_inference_semantics() {
    const auto errors = analyze_source(
        "class Component {\n"
        "  func new(string name) { set this.name = name }\n"
        "}\n"
        "Component c = new Component(\"ok\")\n"
        "string n = c.name\n");

    expect(errors.empty(), "expected set this.field inference to pass semantic analysis");
}

void test_missing_new_constructor_rejects_args() {
    const auto errors = analyze_source(
        "class Box { int value = 0 }\n"
        "Box b = new Box(1)\n");

    expect(contains_error(errors, "has no new() constructor"),
           "expected constructor-argument rejection when new() is missing");
}

}  // namespace

int main() {
    test_valid_semantics();
    test_assignment_type_mismatch();
    test_unknown_function_call();
    test_method_argument_type_mismatch();
    test_fixed_array_initializer_bounds();
    test_dynamic_array_keyword_semantics();
    test_dynamic_array_methods_semantics();
    test_this_field_inference_semantics();
    test_missing_new_constructor_rejects_args();
    std::cout << "All semantic tests passed\n";
    return 0;
}
