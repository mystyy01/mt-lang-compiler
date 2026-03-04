#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast_nodes.hpp"

struct CodegenParameterInfo {
    std::string name;
    std::string llvm_type;
    ASTNode default_value;
    std::string mt_type;

    CodegenParameterInfo() = default;

    CodegenParameterInfo(std::string name_in, std::string llvm_type_in, ASTNode default_value_in = nullptr,
                         std::string mt_type_in = "")
        : name(std::move(name_in)),
          llvm_type(std::move(llvm_type_in)),
          default_value(std::move(default_value_in)),
          mt_type(std::move(mt_type_in)) {}

    CodegenParameterInfo(const CodegenParameterInfo& other)
        : name(other.name),
          llvm_type(other.llvm_type),
          default_value(clone_node(other.default_value)),
          mt_type(other.mt_type) {}

    CodegenParameterInfo& operator=(const CodegenParameterInfo& other) {
        if (this == &other) {
            return *this;
        }
        name = other.name;
        llvm_type = other.llvm_type;
        default_value = clone_node(other.default_value);
        mt_type = other.mt_type;
        return *this;
    }

    CodegenParameterInfo(CodegenParameterInfo&&) noexcept = default;
    CodegenParameterInfo& operator=(CodegenParameterInfo&&) noexcept = default;
};

struct CodegenFunctionInfo {
    std::string name;
    std::string return_type;
    std::string return_mt_type;
    std::vector<CodegenParameterInfo> parameters;
    bool is_external = false;
    bool is_var_arg = false;
};

struct CodegenClassFieldInfo {
    std::string name;
    std::string mt_type;
    std::string element_mt_type = "";
    std::string llvm_type;
    std::size_t offset = 0;
    bool is_constructor_arg = false;
    ASTNode initializer;
};

struct CodegenClassMethodInfo {
    std::string class_name;
    std::string method_name;
    std::string mangled_name;
    std::string return_type;
    std::string return_mt_type;
    std::vector<CodegenParameterInfo> parameters;
    const MethodDeclaration* ast_node = nullptr;
};

struct CodegenClassInfo {
    std::string name;
    std::size_t object_size = 1;
    std::string parent_class;
    int type_tag = 0;
    std::unordered_map<std::string, CodegenClassFieldInfo> fields;
    std::vector<std::string> constructor_arg_fields;
    std::unordered_map<std::string, CodegenClassMethodInfo> methods;
};

class CodeGenerator {
public:
    CodeGenerator(bool emit_main_function = true, bool emit_builtin_decls = true);

    std::string generate(ASTNode& root);

private:
    struct IRValue {
        std::string type;
        std::string value;
        bool is_valid = false;
    };

    struct VariableInfo {
        std::string llvm_type;
        std::string ptr_value;
        bool is_fixed_array = false;
        std::string fixed_array_elem_llvm_type;
        int fixed_array_size = 0;
        bool is_dynamic_array = false;
        std::string dynamic_array_elem_llvm_type;
        std::string class_name;
        bool is_dict = false;
        std::string dict_key_llvm_type;
        std::string dict_value_llvm_type;
        std::string dict_key_mt_type;
        std::string dict_value_mt_type;
        std::string dynamic_array_elem_mt_type = "";
    };

    struct StringConstantInfo {
        std::string symbol;
        std::size_t length = 0;
    };

    struct TryContext {
        std::string saved_prev_jmp_addr;
    };

    std::unordered_map<std::string, CodegenFunctionInfo> functions;
    std::unordered_map<std::string, CodegenClassInfo> classes;
    std::unordered_map<std::string, int> class_type_tags;
    std::unordered_map<std::string, StringConstantInfo> string_constants;
    std::vector<ASTNode> top_level_variables;
    std::unordered_map<std::string, VariableInfo> module_globals;
    std::vector<std::string> global_lines;
    std::vector<std::string> declaration_lines;
    std::vector<std::string> function_lines;

    std::vector<std::unordered_map<std::string, VariableInfo>> variable_scopes;
    std::vector<std::string> break_labels;
    std::vector<TryContext> try_contexts;

    std::string current_function_name;
    std::string current_return_type;
    bool current_block_terminated;
    std::size_t entry_alloca_insert_index;

    int register_counter;
    int label_counter;
    int string_counter;
    bool emit_main_function_flag;
    bool emit_builtin_decls_flag;

    void collect_program_declarations(Program& program);
    void register_function_declaration(FunctionDeclaration& node);
    void register_dynamic_function_declaration(DynamicFunctionDeclaration& node);
    void register_external_declaration(ExternalDeclaration& node);
    void register_libc_import_declaration(LibcImportStatement& node);
    void register_class_declaration(ClassDeclaration& node);

    void emit_prelude();
    void emit_builtin_declarations();
    void emit_user_declarations();

    void emit_function_definition(FunctionDeclaration& node);
    void emit_dynamic_function_definition(DynamicFunctionDeclaration& node);
    void emit_class_method_definition(const CodegenClassMethodInfo& method_info);
    void emit_main_function(Program& program);
    void begin_function(const CodegenFunctionInfo& info);
    void end_function();

    void emit_line(const std::string& line);
    void emit_label(const std::string& label);

    std::string next_register(const std::string& prefix = "tmp");
    std::string next_label(const std::string& prefix);

    void push_scope();
    void pop_scope();
    void declare_variable(const std::string& name, const VariableInfo& info);
    const VariableInfo* lookup_variable(const std::string& name) const;
    const VariableInfo* resolve_variable(const std::string& name);
    bool materialize_top_level_variable(const std::string& name);
    bool is_top_level_variable_name(const std::string& name) const;

    std::string map_type_to_llvm(const std::string& mt_type) const;

    IRValue generate_expression(ASTNode& node);
    void generate_statement(ASTNode& node);

    IRValue generate_number_literal(NumberLiteral& node);
    IRValue generate_float_literal(FloatLiteral& node);
    IRValue generate_string_literal(StringLiteral& node);
    IRValue generate_bool_literal(BoolLiteral& node);
    IRValue generate_array_literal(ArrayLiteral& node,
                                   const std::string& forced_element_mt_type = "");
    IRValue generate_dict_literal(DictLiteral& node);
    IRValue generate_null_literal();
    IRValue generate_identifier(Identifier& node);
    IRValue generate_this_expression(ThisExpression& node);
    IRValue generate_binary_expression(BinaryExpression& node);
    IRValue generate_in_expression(InExpression& node);
    IRValue generate_string_concat(IRValue lhs, IRValue rhs);
    IRValue generate_call_expression(CallExpression& node);
    IRValue generate_typeof_expression(TypeofExpression& node);
    IRValue generate_hasattr_expression(HasattrExpression& node);
    IRValue generate_index_expression(IndexExpression& node);
    IRValue generate_member_expression(MemberExpression& node);
    IRValue generate_new_expression(NewExpression& node);

    void generate_variable_declaration(VariableDeclaration& node);
    void generate_set_statement(SetStatement& node);
    void generate_expression_statement(ExpressionStatement& node);
    void generate_return_statement(ReturnStatement& node);
    void generate_block(Block& node);
    void generate_if_statement(IfStatement& node);
    void generate_while_statement(WhileStatement& node);
    void generate_break_statement(BreakStatement& node);

    void emit_restore_try_contexts_for_return();
    std::string infer_class_name_from_ast(ASTNode& node);
    bool class_is_a(const std::string& derived_class, const std::string& base_class) const;
    int class_tag_for_name(const std::string& class_name) const;

    IRValue cast_value(const IRValue& value, const std::string& target_type);
    IRValue ensure_boolean(const IRValue& value);

    StringConstantInfo get_or_create_string_constant(const std::string& value);
    std::string string_constant_gep(const StringConstantInfo& info) const;
    std::string escape_llvm_string(const std::string& value) const;
    std::string infer_type_name(const IRValue& value) const;
    std::size_t llvm_type_size(const std::string& llvm_type) const;
    std::size_t align_up(std::size_t value, std::size_t alignment) const;
    bool is_builtin_mt_type(const std::string& mt_type) const;

    IRValue emit_call(const std::string& return_type,
                      const std::string& callee,
                      const std::vector<IRValue>& args,
                      bool var_arg = false);
};
