#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast_nodes.hpp"
#include "libc_functions.hpp"

struct FunctionParameterInfo {
    std::string name;
    std::string param_type;
    ASTNode default_value;
    std::string element_type;

    FunctionParameterInfo() = default;

    FunctionParameterInfo(std::string name_in, std::string param_type_in,
                          ASTNode default_value_in = nullptr,
                          std::string element_type_in = "")
        : name(std::move(name_in)),
          param_type(std::move(param_type_in)),
          default_value(std::move(default_value_in)),
          element_type(std::move(element_type_in)) {}

    FunctionParameterInfo(const FunctionParameterInfo& other)
        : name(other.name),
          param_type(other.param_type),
          default_value(clone_node(other.default_value)),
          element_type(other.element_type) {}

    FunctionParameterInfo& operator=(const FunctionParameterInfo& other) {
        if (this == &other) {
            return *this;
        }
        name = other.name;
        param_type = other.param_type;
        default_value = clone_node(other.default_value);
        element_type = other.element_type;
        return *this;
    }

    FunctionParameterInfo(FunctionParameterInfo&&) noexcept = default;
    FunctionParameterInfo& operator=(FunctionParameterInfo&&) noexcept = default;
};

struct SymbolInfo {
    std::string symbol_type;
    std::string data_type;
    std::vector<FunctionParameterInfo> parameters;
    std::string element_type;
    int fixed_size = -1;
};

class SymbolTable {
public:
    SymbolTable();

    void enter_scope();
    void exit_scope();

    void declare(const std::string& name,
                 const std::string& symbol_type,
                 const std::string& data_type,
                 const std::vector<FunctionParameterInfo>& parameters = {},
                 const std::string& element_type = "",
                 int fixed_size = -1);

    const SymbolInfo* lookup(const std::string& name) const;

    const std::vector<std::unordered_map<std::string, SymbolInfo>>& get_scopes() const;

private:
    std::vector<std::unordered_map<std::string, SymbolInfo>> scopes;
};

struct ClassFieldInfo {
    std::string name;
    std::string type;
    bool is_constructor_arg = false;
    std::string element_type;
};

struct ClassMethodInfo {
    std::string return_type;
    std::vector<FunctionParameterInfo> params;
};

struct ClassInfo {
    std::string symbol_type;
    std::string data_type;
    std::vector<ClassFieldInfo> fields;
    std::unordered_map<std::string, ClassMethodInfo> methods;
    std::string parent_class;
};

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(std::string file_path = "unknown");

    std::string analyze(ASTNode& node);

    const std::vector<std::string>& get_errors() const;
    const SymbolTable& get_symbol_table() const;
    const std::unordered_map<std::string, ClassInfo>& get_classes() const;

private:
    SymbolTable symbol_table;
    std::unordered_map<std::string, ClassInfo> classes;
    std::vector<std::string> errors;
    std::string file_path;
    std::string current_class;
    int break_depth;

    void add_builtin_symbols();

    std::string get_position_info(const ASTNode& node) const;
    std::string get_position_info(int line, int column) const;
    void add_error(const std::string& message, bool include_file = true);

    bool is_builtin_type(const std::string& type_name) const;
    bool is_known_type(const std::string& type_name) const;

    std::vector<FunctionParameterInfo> to_param_info(const std::vector<Parameter>& params) const;

    void validate_call_arguments(const std::string& callable_name,
                                 std::vector<ASTNode>& arguments,
                                 const std::vector<FunctionParameterInfo>& parameters,
                                 bool is_method = false);

    std::string analyze_program(Program& node);
    std::string analyze_class_declaration(ClassDeclaration& node);
    std::string analyze_field_declaration(FieldDeclaration& node);
    std::string analyze_method_declaration(MethodDeclaration& node);
    std::string analyze_new_expression(NewExpression& node);
    std::string analyze_index_expression(IndexExpression& node);
    std::string analyze_member_expression(MemberExpression& node);
    std::string analyze_variable_declaration(VariableDeclaration& node);
    std::string analyze_function_declaration(FunctionDeclaration& node);
    std::string analyze_external_declaration(ExternalDeclaration& node);
    std::string analyze_dynamic_function_declaration(DynamicFunctionDeclaration& node);
    std::string analyze_identifier(Identifier& node);
    std::string analyze_expression_statement(ExpressionStatement& node);
    std::string analyze_binary_expression(BinaryExpression& node);
    std::string analyze_in_expression(InExpression& node);
    std::string analyze_if_statement(IfStatement& node);
    std::string analyze_break_statement(BreakStatement& node);
    std::string analyze_for_statement(ForInStatement& node);
    std::string analyze_simple_import(SimpleImportStatement& node);
    std::string analyze_libc_import(LibcImportStatement& node);
    std::string analyze_from_import(FromImportStatement& node);
    std::string analyze_call_expression(CallExpression& node);
    std::string analyze_number_literal(NumberLiteral& node);
    std::string analyze_float_literal(FloatLiteral& node);
    std::string analyze_string_literal(StringLiteral& node);
    std::string analyze_array_literal(ArrayLiteral& node);
    std::string analyze_dict_literal(DictLiteral& node);
    std::string analyze_bool_literal(BoolLiteral& node);
    std::string analyze_null_literal(NullLiteral& node);
    std::string analyze_hasattr_expression(HasattrExpression& node);
    std::string analyze_while_statement(WhileStatement& node);
    std::string analyze_for_in_statement(ForInStatement& node);
    std::string analyze_set_statement(SetStatement& node);
    std::string analyze_return_statement(ReturnStatement& node);
    std::string analyze_block(Block& node);
    std::string analyze_try_statement(TryStatement& node);
    std::string analyze_catch_block(CatchBlock& node, std::vector<std::string>* catch_types_seen = nullptr);
    std::string analyze_throw_statement(ThrowStatement& node);

    bool flatten_module_path(ASTNode& module_path, std::vector<std::string>* parts) const;
};
