#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/codegen.hpp"
#include "../src/parser.hpp"
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
    std::string file_path = "tests/codegen_input.mtc";
    Tokenizer tokenizer(source, file_path);
    std::vector<Token> tokens = tokenizer.tokenize();
    Parser parser(std::move(tokens), file_path);
    return parser.parse_program();
}

std::string generate_ir(const std::string& source_text) {
    ASTNode root = parse_source(source_text);
    CodeGenerator generator;
    return generator.generate(root);
}

void verify_ir_syntax(const std::string& ir, const std::string& tag) {
    const std::string ir_path = "/tmp/mt_codegen_" + tag + ".ll";
    const std::string log_path = "/tmp/mt_codegen_" + tag + ".log";

    {
        std::ofstream out(ir_path);
        expect(out.is_open(), "failed to open temporary IR file: " + ir_path);
        out << ir;
    }

    const std::string cmd = "clang -x ir -c " + ir_path + " -o /tmp/mt_codegen_" + tag +
                            ".o >" + log_path + " 2>&1";
    const int status = std::system(cmd.c_str());
    if (status != 0) {
        std::ifstream log(log_path);
        std::stringstream buffer;
        buffer << log.rdbuf();
        std::cerr << "Generated IR:\n" << ir << "\n";
        std::cerr << "clang diagnostics:\n" << buffer.str() << "\n";
    }

    expect(status == 0, "clang rejected generated IR for test tag: " + tag);
}

void test_function_and_print_codegen() {
    const std::string ir = generate_ir(
        "int add(int a, int b) {\n"
        "  return a + b\n"
        "}\n"
        "int x = add(40, 2)\n"
        "print(x)\n");

    expect(ir.find("define i32 @add") != std::string::npos,
           "expected add function definition in IR");
    expect(ir.find("call i32 @add") != std::string::npos,
           "expected call to add function in IR");
    expect(ir.find("call i32 @printf") != std::string::npos,
           "expected printf call in IR");

    verify_ir_syntax(ir, "func_print");
}

void test_control_flow_codegen() {
    const std::string ir = generate_ir(
        "for (j in range(5)) {\n"
        "  if (j == 3) {\n"
        "    break\n"
        "  }\n"
        "  print(j)\n"
        "}\n"
        "int i = 0\n"
        "while (i < 2) {\n"
        "  set i = i + 1\n"
        "}\n");

    expect(ir.find("for_cond") != std::string::npos,
           "expected for-loop labels in IR");
    expect(ir.find("while_cond") != std::string::npos,
           "expected while-loop labels in IR");

    verify_ir_syntax(ir, "control_flow");
}

void test_conversion_codegen() {
    const std::string ir = generate_ir(
        "int x = int(\"42\")\n"
        "float y = float(\"3.5\")\n"
        "string s = str(x)\n"
        "print(s)\n");

    expect(ir.find("call i64 @strtol") != std::string::npos,
           "expected strtol usage in IR");
    expect(ir.find("call double @atof") != std::string::npos,
           "expected atof usage in IR");
    expect(ir.find("call i32 @sprintf") != std::string::npos,
           "expected sprintf usage in IR");

    verify_ir_syntax(ir, "conversions");
}

void test_fixed_stack_array_codegen() {
    const std::string ir = generate_ir(
        "int[5] nums = [1, 2, 3]\n"
        "set nums[3] = 9\n"
        "print(nums[3])\n");

    expect(ir.find("alloca [5 x i32]") != std::string::npos,
           "expected fixed-size stack array alloca in IR");
    expect(ir.find("getelementptr inbounds [5 x i32]") != std::string::npos,
           "expected indexed GEP for fixed-size array in IR");

    verify_ir_syntax(ir, "fixed_stack_array");
}

void test_default_array_literal_is_stack_codegen() {
    const std::string ir = generate_ir(
        "array<int> nums = [1, 2, 3]\n"
        "print(nums[1])\n");

    expect(ir.find("alloca [3 x i32]") != std::string::npos,
           "expected non-dynamic array literal declaration to be stack allocated");
    expect(ir.find("call i8* @malloc(i64 24)") == std::string::npos,
           "expected non-dynamic array literal declaration to avoid heap header allocation");

    verify_ir_syntax(ir, "default_array_stack");
}

void test_dynamic_array_keyword_codegen() {
    const std::string ir = generate_ir(
        "dynamic array nums = [0, 1, 2, 3]\n"
        "nums.append(4)\n"
        "int last = nums.pop()\n"
        "print(last)\n"
        "print(nums.length())\n"
        "print(length(nums))\n");

    expect(ir.find("call i8* @malloc(i64 24)") != std::string::npos,
           "expected dynamic array header allocation in IR");
    expect(ir.find("store i64 4") != std::string::npos,
           "expected dynamic array length initialization in IR");
    expect(ir.find("call i8* @realloc") != std::string::npos,
           "expected dynamic array growth with realloc in IR");

    verify_ir_syntax(ir, "dynamic_array_keyword");
}

void test_class_codegen() {
    const std::string ir = generate_ir(
        "class Counter {\n"
        "  arg int seed\n"
        "  int value = 0\n"
        "  int next(int step) {\n"
        "    set this.value = this.value + step\n"
        "    return this.value\n"
        "  }\n"
        "}\n"
        "Counter c = new Counter(10)\n"
        "print(c.value)\n"
        "print(c.next(2))\n");

    expect(ir.find("define i32 @Counter__next") != std::string::npos,
           "expected class method definition in IR");
    expect(ir.find("call i8* @malloc") != std::string::npos,
           "expected object heap allocation in IR");
    expect(ir.find("call i32 @Counter__next") != std::string::npos,
           "expected class method call in IR");

    verify_ir_syntax(ir, "class_codegen");
}

}  // namespace

int main() {
    test_function_and_print_codegen();
    test_control_flow_codegen();
    test_conversion_codegen();
    test_fixed_stack_array_codegen();
    test_default_array_literal_is_stack_codegen();
    test_dynamic_array_keyword_codegen();
    test_class_codegen();
    std::cout << "All codegen tests passed\n";
    return 0;
}
