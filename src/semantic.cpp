#include "semantic.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "parser.hpp"
#include "tokenizer.hpp"

namespace {

std::filesystem::path path_from_parts(const std::vector<std::string>& parts) {
    std::filesystem::path result;
    for (const auto& part : parts) {
        result /= part;
    }
    result += ".mtc";
    return result;
}

bool read_file(const std::filesystem::path& path, std::string* out) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }
    out->assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

SymbolTable::SymbolTable() {
    enter_scope();
}

void SymbolTable::enter_scope() {
    scopes.emplace_back();
}

void SymbolTable::exit_scope() {
    if (!scopes.empty()) {
        scopes.pop_back();
    }
}

void SymbolTable::declare(const std::string& name,
                          const std::string& symbol_type,
                          const std::string& data_type,
                          const std::vector<FunctionParameterInfo>& parameters,
                          const std::string& element_type,
                          int fixed_size) {
    if (scopes.empty()) {
        enter_scope();
    }

    scopes.back()[name] = SymbolInfo{
        symbol_type,
        data_type,
        parameters,
        element_type,
        fixed_size,
    };
}

const SymbolInfo* SymbolTable::lookup(const std::string& name) const {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        const auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

const std::vector<std::unordered_map<std::string, SymbolInfo>>& SymbolTable::get_scopes() const {
    return scopes;
}

SemanticAnalyzer::SemanticAnalyzer(std::string file_path_in)
    : file_path(std::move(file_path_in)), break_depth(0) {
    add_builtin_symbols();
}

const std::vector<std::string>& SemanticAnalyzer::get_errors() const {
    return errors;
}

const SymbolTable& SemanticAnalyzer::get_symbol_table() const {
    return symbol_table;
}

const std::unordered_map<std::string, ClassInfo>& SemanticAnalyzer::get_classes() const {
    return classes;
}

void SemanticAnalyzer::add_builtin_symbols() {
    symbol_table.declare("print", "builtin", "void");
    symbol_table.declare("length", "builtin", "int");
    symbol_table.declare("append", "builtin", "void");
    symbol_table.declare("pop", "builtin", "any");
    symbol_table.declare("str", "builtin", "string");
    symbol_table.declare("int", "builtin", "int");
    symbol_table.declare("float", "builtin", "float");
    symbol_table.declare("read", "builtin", "string");
    symbol_table.declare("split", "builtin", "array");

    for (const auto& [name, info] : LIBC_FUNCTIONS) {
        symbol_table.declare(name, "builtin", libc_return_type_to_semantic_type(info.ret));
    }
}

std::string SemanticAnalyzer::get_position_info(const ASTNode& node) const {
    const int line = get_node_line(node);
    const int column = get_node_column(node);
    return get_position_info(line, column);
}

std::string SemanticAnalyzer::get_position_info(int line, int column) const {
    if (line < 0) {
        return "";
    }
    std::string info = " at line " + std::to_string(line);
    if (column >= 0) {
        info += ", column " + std::to_string(column);
    }
    return info;
}

void SemanticAnalyzer::add_error(const std::string& message, bool include_file) {
    if (include_file && !file_path.empty() && file_path != "unknown") {
        errors.push_back(message + " in " + file_path);
        return;
    }
    errors.push_back(message);
}

bool SemanticAnalyzer::is_builtin_type(const std::string& type_name) const {
    static const std::unordered_set<std::string> kBuiltinTypes = {
        "int", "float", "string", "bool", "array", "dict", "void", "any", "null",
    };
    return kBuiltinTypes.count(type_name) > 0;
}

bool SemanticAnalyzer::is_known_type(const std::string& type_name) const {
    if (type_name.empty()) {
        return false;
    }
    if (is_builtin_type(type_name)) {
        return true;
    }
    if (classes.find(type_name) != classes.end()) {
        return true;
    }
    const SymbolInfo* symbol = symbol_table.lookup(type_name);
    return symbol != nullptr && symbol->symbol_type == "class";
}

std::vector<FunctionParameterInfo> SemanticAnalyzer::to_param_info(const std::vector<Parameter>& params) const {
    std::vector<FunctionParameterInfo> out;
    out.reserve(params.size());
    for (const auto& param : params) {
        out.emplace_back(
            param.name,
            param.param_type.empty() ? "any" : param.param_type,
            clone_node(param.default_value),
            param.element_type);
    }
    return out;
}

void SemanticAnalyzer::validate_call_arguments(const std::string& callable_name,
                                               std::vector<ASTNode>& arguments,
                                               const std::vector<FunctionParameterInfo>& parameters,
                                               bool is_method) {
    while (arguments.size() < parameters.size() &&
           parameters[arguments.size()].default_value != nullptr) {
        arguments.push_back(clone_node(parameters[arguments.size()].default_value));
    }

    const std::size_t num_args = arguments.size();
    const std::size_t num_params = parameters.size();

    std::size_t min_args = 0;
    for (const auto& param : parameters) {
        if (!param.default_value) {
            ++min_args;
        }
    }

    if (num_args < min_args || num_args > num_params) {
        add_error(
            std::string(is_method ? "Method '" : "Function '") + callable_name +
            "' expects " + std::to_string(min_args) + " to " +
            std::to_string(num_params) + " arguments, got " +
            std::to_string(num_args));
    }

    for (std::size_t i = 0; i < arguments.size() && i < parameters.size(); ++i) {
        const std::string expected = parameters[i].param_type.empty()
                                     ? "any"
                                     : parameters[i].param_type;
        const std::string arg_type = analyze(arguments[i]);

        if (expected != "any" && arg_type != expected && arg_type != "any" && arg_type != "null") {
            std::string pos_info;
            if (arguments[i]) {
                pos_info = get_position_info(arguments[i]);
            }
            const std::string callable_label = is_method ? "method '" : "function '";
            add_error(
                "Argument " + std::to_string(i + 1) + " of " +
                callable_label + callable_name +
                "' expects " + expected + ", got " + arg_type + pos_info);
        }
    }
}

std::string SemanticAnalyzer::analyze(ASTNode& node) {
    if (!node) {
        return "";
    }

    if (is_node<Program>(node)) {
        return analyze_program(get_node<Program>(node));
    }
    if (is_node<ClassDeclaration>(node)) {
        return analyze_class_declaration(get_node<ClassDeclaration>(node));
    }
    if (is_node<VariableDeclaration>(node)) {
        return analyze_variable_declaration(get_node<VariableDeclaration>(node));
    }
    if (is_node<FunctionDeclaration>(node)) {
        return analyze_function_declaration(get_node<FunctionDeclaration>(node));
    }
    if (is_node<ExternalDeclaration>(node)) {
        return analyze_external_declaration(get_node<ExternalDeclaration>(node));
    }
    if (is_node<DynamicFunctionDeclaration>(node)) {
        return analyze_dynamic_function_declaration(get_node<DynamicFunctionDeclaration>(node));
    }
    if (is_node<FromImportStatement>(node)) {
        return analyze_from_import(get_node<FromImportStatement>(node));
    }
    if (is_node<LibcImportStatement>(node)) {
        return analyze_libc_import(get_node<LibcImportStatement>(node));
    }
    if (is_node<SimpleImportStatement>(node)) {
        return analyze_simple_import(get_node<SimpleImportStatement>(node));
    }
    if (is_node<NumberLiteral>(node)) {
        return analyze_number_literal(get_node<NumberLiteral>(node));
    }
    if (is_node<FloatLiteral>(node)) {
        return analyze_float_literal(get_node<FloatLiteral>(node));
    }
    if (is_node<ArrayLiteral>(node)) {
        return analyze_array_literal(get_node<ArrayLiteral>(node));
    }
    if (is_node<DictLiteral>(node)) {
        return analyze_dict_literal(get_node<DictLiteral>(node));
    }
    if (is_node<StringLiteral>(node)) {
        return analyze_string_literal(get_node<StringLiteral>(node));
    }
    if (is_node<NullLiteral>(node)) {
        return analyze_null_literal(get_node<NullLiteral>(node));
    }
    if (is_node<NewExpression>(node)) {
        return analyze_new_expression(get_node<NewExpression>(node));
    }
    if (is_node<Identifier>(node)) {
        return analyze_identifier(get_node<Identifier>(node));
    }
    if (is_node<CallExpression>(node)) {
        return analyze_call_expression(get_node<CallExpression>(node));
    }
    if (is_node<BinaryExpression>(node)) {
        return analyze_binary_expression(get_node<BinaryExpression>(node));
    }
    if (is_node<InExpression>(node)) {
        return analyze_in_expression(get_node<InExpression>(node));
    }
    if (is_node<IndexExpression>(node)) {
        return analyze_index_expression(get_node<IndexExpression>(node));
    }
    if (is_node<MemberExpression>(node)) {
        return analyze_member_expression(get_node<MemberExpression>(node));
    }
    if (is_node<ExpressionStatement>(node)) {
        return analyze_expression_statement(get_node<ExpressionStatement>(node));
    }
    if (is_node<WhileStatement>(node)) {
        return analyze_while_statement(get_node<WhileStatement>(node));
    }
    if (is_node<IfStatement>(node)) {
        return analyze_if_statement(get_node<IfStatement>(node));
    }
    if (is_node<ForInStatement>(node)) {
        return analyze_for_in_statement(get_node<ForInStatement>(node));
    }
    if (is_node<SetStatement>(node)) {
        return analyze_set_statement(get_node<SetStatement>(node));
    }
    if (is_node<ReturnStatement>(node)) {
        return analyze_return_statement(get_node<ReturnStatement>(node));
    }
    if (is_node<Block>(node)) {
        return analyze_block(get_node<Block>(node));
    }
    if (is_node<TryStatement>(node)) {
        return analyze_try_statement(get_node<TryStatement>(node));
    }
    if (is_node<ThrowStatement>(node)) {
        return analyze_throw_statement(get_node<ThrowStatement>(node));
    }
    if (is_node<BreakStatement>(node)) {
        return analyze_break_statement(get_node<BreakStatement>(node));
    }
    if (is_node<BoolLiteral>(node)) {
        return analyze_bool_literal(get_node<BoolLiteral>(node));
    }
    if (is_node<TypeofExpression>(node)) {
        auto& expr = get_node<TypeofExpression>(node);
        analyze(expr.argument);
        return "string";
    }
    if (is_node<HasattrExpression>(node)) {
        return analyze_hasattr_expression(get_node<HasattrExpression>(node));
    }
    if (is_node<ClassofExpression>(node)) {
        auto& expr = get_node<ClassofExpression>(node);
        analyze(expr.argument);
        return "string";
    }

    return "unknown";
}

std::string SemanticAnalyzer::analyze_program(Program& node) {
    for (auto& statement : node.statements) {
        analyze(statement);
    }
    return "";
}

std::string SemanticAnalyzer::analyze_class_declaration(ClassDeclaration& node) {
    symbol_table.declare(node.name, "class", node.name);

    const std::string previous_class = current_class;
    current_class = node.name;

    ClassInfo class_info;
    class_info.symbol_type = "class";
    class_info.data_type = node.name;
    class_info.parent_class = node.inherits_from;

    for (const auto& field : node.fields) {
        class_info.fields.push_back(ClassFieldInfo{
            field.name,
            field.type,
            field.is_constructor_arg,
            field.element_type,
        });
    }

    for (const auto& method : node.methods) {
        class_info.methods[method.name] = ClassMethodInfo{
            method.return_type.empty() ? "any" : method.return_type,
            to_param_info(method.params),
        };
    }

    classes[node.name] = class_info;

    symbol_table.enter_scope();

    for (auto& field : node.fields) {
        analyze_field_declaration(field);
    }

    for (auto& method : node.methods) {
        analyze_method_declaration(method);
    }

    symbol_table.exit_scope();

    current_class = previous_class;
    return "";
}

std::string SemanticAnalyzer::analyze_field_declaration(FieldDeclaration& node) {
    symbol_table.declare(node.name, "field", node.type, {}, node.element_type);

    if (node.initializer) {
        const std::string init_type = analyze(node.initializer);
        if (init_type != node.type && init_type != "any" && init_type != "null") {
            add_error("Field initializer type mismatch" + get_position_info(node.line, node.column) +
                      ". Cannot assign " + init_type + " to " + node.type);
        }
    }

    return "";
}

std::string SemanticAnalyzer::analyze_method_declaration(MethodDeclaration& node) {
    symbol_table.declare(node.name, "method", node.return_type.empty() ? "any" : node.return_type);

    symbol_table.enter_scope();
    symbol_table.declare("this", "parameter", current_class);

    for (const auto& param : node.params) {
        symbol_table.declare(param.name, "parameter",
                             param.param_type.empty() ? "any" : param.param_type,
                             {}, param.element_type);
    }

    if (node.body && is_node<Block>(node.body)) {
        auto& block = get_node<Block>(node.body);
        for (auto& statement : block.statements) {
            analyze(statement);
        }
    }

    symbol_table.exit_scope();
    return "";
}

std::string SemanticAnalyzer::analyze_new_expression(NewExpression& node) {
    const SymbolInfo* class_symbol = symbol_table.lookup(node.class_name);
    if (!class_symbol || class_symbol->symbol_type != "class") {
        add_error("Unknown class '" + node.class_name + "'" +
                  get_position_info(node.line, node.column));
        return "unknown";
    }
    return node.class_name;
}

std::string SemanticAnalyzer::analyze_index_expression(IndexExpression& node) {
    const std::string obj_type = analyze(node.object);
    if (obj_type.empty()) {
        return "";
    }
    const std::string pos_info = node.object ? get_position_info(node.object) : "";

    const std::string index_type = analyze(node.index);

    if (obj_type == "dict" || obj_type.rfind("dict<", 0) == 0) {
        if (index_type != "string" && index_type != "any" && index_type != "int") {
            add_error("Dict key must be a valid type, got " + index_type + pos_info);
        }
        return "any";
    }

    if (obj_type == "any") {
        return "any";
    }

    if (obj_type == "string" || obj_type == "array") {
        if (index_type != "int" && index_type != "any") {
            add_error("Array/string index must be an integer" + pos_info);
            return "unknown";
        }
    }

    if (obj_type == "string") {
        return "string";
    }

    if (obj_type == "array") {
        if (node.object && is_node<Identifier>(node.object)) {
            const auto& identifier = get_node<Identifier>(node.object);
            const SymbolInfo* symbol = symbol_table.lookup(identifier.name);
            if (symbol && !symbol->element_type.empty()) {
                if (symbol->fixed_size > 0 && node.index && is_node<NumberLiteral>(node.index)) {
                    const int index_value = get_node<NumberLiteral>(node.index).value;
                    if (index_value < 0 || index_value >= symbol->fixed_size) {
                        add_error("Array index out of bounds for '" + identifier.name +
                                  "' (size " + std::to_string(symbol->fixed_size) + ")" + pos_info);
                    }
                }
                return symbol->element_type;
            }
        }
        if (node.object && is_node<MemberExpression>(node.object)) {
            const auto& member = get_node<MemberExpression>(node.object);
            const SymbolInfo* symbol = symbol_table.lookup(member.property);
            if (symbol && !symbol->element_type.empty()) {
                return symbol->element_type;
            }
        }
        return "any";
    }

    add_error("Cannot index into type '" + obj_type + "'" + pos_info);
    return "unknown";
}

std::string SemanticAnalyzer::analyze_member_expression(MemberExpression& node) {
    const std::string object_type = analyze(node.object);

    if (node.property == "length" && (object_type == "string" || object_type == "array")) {
        return "int";
    }

    if (node.object && is_node<ThisExpression>(node.object) && !current_class.empty()) {
        const auto class_it = classes.find(current_class);
        if (class_it != classes.end()) {
            for (const auto& field : class_it->second.fields) {
                if (field.name == node.property) {
                    return field.type;
                }
            }
            if (class_it->second.methods.find(node.property) != class_it->second.methods.end()) {
                return "any";
            }
        }
    }

    if (node.object && is_node<Identifier>(node.object)) {
        const auto& identifier = get_node<Identifier>(node.object);
        const SymbolInfo* var_symbol = symbol_table.lookup(identifier.name);
        if (var_symbol && var_symbol->symbol_type == "variable") {
            const auto class_it = classes.find(var_symbol->data_type);
            if (class_it != classes.end()) {
                for (const auto& field : class_it->second.fields) {
                    if (field.name == node.property) {
                        return field.type;
                    }
                }
            }
        }
    }

    if (object_type == "module") {
        return "any";
    }

    if (node.object && is_node<CallExpression>(node.object)) {
        const auto class_it = classes.find(object_type);
        if (class_it != classes.end()) {
            for (const auto& field : class_it->second.fields) {
                if (field.name == node.property) {
                    return field.type;
                }
            }
        }
        return "any";
    }

    return object_type;
}

std::string SemanticAnalyzer::analyze_variable_declaration(VariableDeclaration& node) {
    if (!is_known_type(node.type)) {
        add_error("Unknown type '" + node.type + "'" + get_position_info(node.line, node.column));
    }

    if (!node.element_type.empty() && !is_known_type(node.element_type)) {
        add_error("Unknown element type '" + node.element_type + "'" +
                  get_position_info(node.line, node.column));
    }

    if (!node.key_type.empty() && !is_known_type(node.key_type)) {
        add_error("Unknown dict key type '" + node.key_type + "'" +
                  get_position_info(node.line, node.column));
    }

    if (!node.value_type.empty() && !is_known_type(node.value_type) && node.value_type != "any") {
        add_error("Unknown dict value type '" + node.value_type + "'" +
                  get_position_info(node.line, node.column));
    }

    if (node.fixed_size != -1) {
        if (node.type != "array") {
            add_error("Fixed-size array syntax is only valid for array declarations" +
                      get_position_info(node.line, node.column));
        }
        if (node.type == "array" && node.element_type.empty()) {
            add_error("Fixed-size array declarations require an element type" +
                      get_position_info(node.line, node.column));
        }
        if (node.fixed_size <= 0) {
            add_error("Fixed-size array must have a positive size" +
                      get_position_info(node.line, node.column));
        }
        if (node.value && is_node<ArrayLiteral>(node.value)) {
            const auto& array_literal = get_node<ArrayLiteral>(node.value);
            if (static_cast<int>(array_literal.elements.size()) > node.fixed_size) {
                add_error("Array initializer has too many elements for fixed size " +
                          std::to_string(node.fixed_size) +
                          get_position_info(node.line, node.column));
            }
        }
    }

    if (node.value) {
        if (node.type == "array" && node.element_type.empty() && is_node<ArrayLiteral>(node.value)) {
            auto& arr = get_node<ArrayLiteral>(node.value);
            if (!arr.elements.empty()) {
                const std::string inferred = analyze(arr.elements[0]);
                if (!inferred.empty() && inferred != "unknown") {
                    node.element_type = inferred;
                }
            }
        }

        const std::string value_type = analyze(node.value);

        std::string expected_type = node.type;
        if (node.type == "dict" && !node.key_type.empty() && !node.value_type.empty()) {
            expected_type = "dict<" + node.key_type + ", " + node.value_type + ">";
        } else if (node.type == "dict") {
            expected_type = value_type;
        }

        if (value_type != expected_type && value_type != "any" && node.type != "any") {
            bool allowed = false;
            if (node.value && is_node<NewExpression>(node.value)) {
                const auto& new_expr = get_node<NewExpression>(node.value);
                allowed = (new_expr.class_name == node.type);
            }
            if (node.value && is_node<CallExpression>(node.value) && value_type == expected_type) {
                allowed = true;
            }
            if (value_type == "null") {
                allowed = true;
            }

            if (!allowed) {
                add_error("Type mismatch" + get_position_info(node.line, node.column) +
                          ". Cannot assign type " + value_type + " to " + expected_type);
            }
        }
    }

    symbol_table.declare(node.name, "variable", node.type, {}, node.element_type, node.fixed_size);
    return "";
}

std::string SemanticAnalyzer::analyze_function_declaration(FunctionDeclaration& node) {
    const auto parameters = to_param_info(node.parameters);
    symbol_table.declare(node.name, "function", node.return_type, parameters);

    symbol_table.enter_scope();
    for (const auto& param : node.parameters) {
        symbol_table.declare(param.name, "parameter",
                             param.param_type.empty() ? "any" : param.param_type,
                             {}, param.element_type);
    }

    if (node.body && is_node<Block>(node.body)) {
        auto& block = get_node<Block>(node.body);
        for (auto& statement : block.statements) {
            analyze(statement);
        }
    }

    symbol_table.exit_scope();
    return "";
}

std::string SemanticAnalyzer::analyze_external_declaration(ExternalDeclaration& node) {
    symbol_table.declare(node.name, "function", node.return_type, to_param_info(node.parameters));
    return "";
}

std::string SemanticAnalyzer::analyze_dynamic_function_declaration(DynamicFunctionDeclaration& node) {
    auto parameters = to_param_info(node.parameters);
    symbol_table.declare(node.name, "function", "any", parameters);

    symbol_table.enter_scope();

    for (auto& param : node.parameters) {
        if (param.param_type.empty()) {
            add_error("Dynamic function '" + node.name + "' requires type annotation for parameter '" +
                      param.name + "'");
            param.param_type = "int";
        }
        symbol_table.declare(param.name, "parameter", param.param_type, {}, param.element_type);
    }

    if (node.body && is_node<Block>(node.body)) {
        auto& block = get_node<Block>(node.body);
        for (auto& statement : block.statements) {
            analyze(statement);
        }
    }

    symbol_table.exit_scope();
    return "any";
}

std::string SemanticAnalyzer::analyze_identifier(Identifier& node) {
    const SymbolInfo* symbol = symbol_table.lookup(node.name);
    if (!symbol) {
        add_error("Undeclared variable '" + node.name + "'" +
                  get_position_info(node.line, node.column));
        return "unknown";
    }
    return symbol->data_type;
}

std::string SemanticAnalyzer::analyze_expression_statement(ExpressionStatement& node) {
    analyze(node.expression);
    return "";
}

std::string SemanticAnalyzer::analyze_binary_expression(BinaryExpression& node) {
    const std::string left_type = analyze(node.left);
    const std::string right_type = analyze(node.right);

    if (node.op.value == "==" || node.op.value == "!=" || node.op.value == ">" ||
        node.op.value == "<" || node.op.value == ">=" || node.op.value == "<=") {
        return "bool";
    }

    if (node.op.value == "&&" || node.op.value == "||") {
        return "bool";
    }

    if (left_type == "float" || right_type == "float") {
        return "float";
    }
    if (left_type == "int" || right_type == "int") {
        return "int";
    }
    if (left_type == "bool" || right_type == "bool") {
        return "bool";
    }

    return "any";
}

std::string SemanticAnalyzer::analyze_in_expression(InExpression& node) {
    analyze(node.item);
    analyze(node.container);
    return "bool";
}

std::string SemanticAnalyzer::analyze_if_statement(IfStatement& node) {
    analyze(node.condition);

    symbol_table.enter_scope();
    if (node.then_body && is_node<Block>(node.then_body)) {
        auto& then_block = get_node<Block>(node.then_body);
        for (auto& statement : then_block.statements) {
            analyze(statement);
        }
    }
    symbol_table.exit_scope();

    if (node.else_body && is_node<Block>(node.else_body)) {
        symbol_table.enter_scope();
        auto& else_block = get_node<Block>(node.else_body);
        for (auto& statement : else_block.statements) {
            analyze(statement);
        }
        symbol_table.exit_scope();
    }

    return "";
}

std::string SemanticAnalyzer::analyze_break_statement(BreakStatement&) {
    ++break_depth;
    return "";
}

std::string SemanticAnalyzer::analyze_for_statement(ForInStatement& node) {
    analyze(node.iterable);

    symbol_table.enter_scope();
    symbol_table.declare(node.variable, "variable", "any");

    if (node.body && is_node<Block>(node.body)) {
        auto& body = get_node<Block>(node.body);
        for (auto& statement : body.statements) {
            analyze(statement);
        }
    }

    symbol_table.exit_scope();
    return "";
}

std::string SemanticAnalyzer::analyze_simple_import(SimpleImportStatement& node) {
    if (!node.alias.empty()) {
        symbol_table.declare(node.alias, "import", "module");
    } else {
        symbol_table.declare(node.module_name, "import", "module");
    }
    return "";
}

std::string SemanticAnalyzer::analyze_libc_import(LibcImportStatement& node) {
    for (const auto& func_name : node.symbols) {
        const auto it = LIBC_FUNCTIONS.find(func_name);
        if (it == LIBC_FUNCTIONS.end()) {
            add_error("Unknown libc function '" + func_name + "'");
            continue;
        }
        symbol_table.declare(func_name, "builtin", libc_return_type_to_semantic_type(it->second.ret));
    }
    return "";
}

bool SemanticAnalyzer::flatten_module_path(ASTNode& module_path,
                                           std::vector<std::string>* parts) const {
    if (!module_path || parts == nullptr) {
        return false;
    }

    if (is_node<Identifier>(module_path)) {
        parts->push_back(get_node<Identifier>(module_path).name);
        return true;
    }

    if (is_node<MemberExpression>(module_path)) {
        auto& member = get_node<MemberExpression>(module_path);
        if (!flatten_module_path(member.object, parts)) {
            return false;
        }
        parts->push_back(member.property);
        return true;
    }

    return false;
}

std::string SemanticAnalyzer::analyze_from_import(FromImportStatement& node) {
    std::vector<std::string> parts;
    if (!flatten_module_path(node.module_path, &parts)) {
        return "";
    }

    const std::filesystem::path rel_path = path_from_parts(parts);

    std::vector<std::filesystem::path> candidates;
    candidates.push_back(rel_path);

    if (!file_path.empty() && file_path != "unknown") {
        const auto base = std::filesystem::path(file_path).parent_path();
        if (!base.empty()) {
            candidates.push_back(base / rel_path);
        }
    }

    candidates.push_back(std::filesystem::path("/mnt/ssd/Coding/mt-lang/compiler") / rel_path);

    std::string module_source;
    std::filesystem::path module_file_path;

    for (const auto& candidate : candidates) {
        if (read_file(candidate, &module_source)) {
            module_file_path = candidate;
            break;
        }
    }

    if (module_source.empty()) {
        if (node.is_wildcard) {
            add_error("Could not load module '" + rel_path.string() + "' for wildcard import");
            return "";
        }

        for (const auto& symbol : node.symbols) {
            symbol_table.declare(symbol, "function", "any");
        }
        return "";
    }

    try {
        std::string file_name = module_file_path.string();
        Tokenizer tokenizer(module_source, file_name);
        std::vector<Token> tokens = tokenizer.tokenize();

        Parser parser(std::move(tokens), file_name);
        ASTNode module_ast = parser.parse_program();

        SemanticAnalyzer module_analyzer(file_name);
        module_analyzer.analyze(module_ast);

        for (const auto& [class_name, class_info] : module_analyzer.get_classes()) {
            classes[class_name] = class_info;
        }

        std::vector<std::pair<std::string, SymbolInfo>> symbols_to_import;

        if (node.is_wildcard) {
            for (const auto& scope : module_analyzer.get_symbol_table().get_scopes()) {
                for (const auto& [name, info] : scope) {
                    if (info.symbol_type == "class" || info.symbol_type == "function") {
                        symbols_to_import.push_back({name, info});
                    }
                }
            }
        } else {
            for (const auto& symbol : node.symbols) {
                const SymbolInfo* imported = module_analyzer.get_symbol_table().lookup(symbol);
                if (imported) {
                    symbols_to_import.push_back({symbol, *imported});
                }
            }
        }

        for (const auto& [name, info] : symbols_to_import) {
            const SymbolInfo* existing = symbol_table.lookup(name);
            if (existing != nullptr && existing->symbol_type != "builtin") {
                add_error("Import conflict: '" + name + "' is already defined");
                continue;
            }

            if (info.symbol_type == "class") {
                symbol_table.declare(name, "class", name);
                auto class_it = module_analyzer.get_classes().find(name);
                if (class_it != module_analyzer.get_classes().end()) {
                    classes[name] = class_it->second;
                }
            } else {
                symbol_table.declare(name, "function", info.data_type, info.parameters,
                                     info.element_type);
            }
        }
    } catch (...) {
        if (node.is_wildcard) {
            add_error("Could not load module '" + rel_path.string() + "' for wildcard import");
        } else {
            for (const auto& symbol : node.symbols) {
                symbol_table.declare(symbol, "function", "any");
            }
        }
    }

    return "";
}

std::string SemanticAnalyzer::analyze_call_expression(CallExpression& node) {
    if (node.callee && is_node<TypeLiteral>(node.callee)) {
        const auto& type_lit = get_node<TypeLiteral>(node.callee);
        for (auto& arg : node.arguments) {
            analyze(arg);
        }
        return type_lit.name;
    }

    if (node.callee && is_node<Identifier>(node.callee)) {
        const std::string func_name = get_node<Identifier>(node.callee).name;
        const SymbolInfo* symbol = symbol_table.lookup(func_name);

        if (!symbol) {
            add_error("Unknown function '" + func_name + "'" +
                      get_position_info(node.line, node.column));
            return "unknown";
        }

        if (symbol->symbol_type == "builtin") {
            if (func_name == "length" && node.arguments.size() != 1) {
                add_error("length() expects exactly 1 argument" +
                          get_position_info(node.line, node.column));
            }
            if (func_name == "append" && node.arguments.size() != 2) {
                add_error("append() expects exactly 2 arguments" +
                          get_position_info(node.line, node.column));
            }
            if (func_name == "pop" && node.arguments.size() != 1) {
                add_error("pop() expects exactly 1 argument" +
                          get_position_info(node.line, node.column));
            }
            if (func_name == "split" && node.arguments.size() != 2) {
                add_error("split() expects exactly 2 arguments" +
                          get_position_info(node.line, node.column));
            }
            for (auto& arg : node.arguments) {
                analyze(arg);
            }
            return symbol->data_type;
        }

        validate_call_arguments(func_name, node.arguments, symbol->parameters, false);
        return symbol->data_type.empty() ? "unknown" : symbol->data_type;
    }

    for (auto& arg : node.arguments) {
        analyze(arg);
    }

    if (node.callee && is_node<MemberExpression>(node.callee)) {
        auto& member = get_node<MemberExpression>(node.callee);
        const std::string method_name = member.property;

        if (member.object && is_node<StringLiteral>(member.object) && method_name == "length") {
            return "int";
        }

        if (member.object && is_node<ThisExpression>(member.object) && !current_class.empty()) {
            const auto class_it = classes.find(current_class);
            if (class_it != classes.end()) {
                const auto method_it = class_it->second.methods.find(method_name);
                if (method_it != class_it->second.methods.end()) {
                    validate_call_arguments(method_name, node.arguments,
                                            method_it->second.params, true);
                    return method_it->second.return_type.empty()
                               ? "any"
                               : method_it->second.return_type;
                }
            }
        }

        if (member.object && is_node<Identifier>(member.object)) {
            const auto& obj = get_node<Identifier>(member.object);
            const SymbolInfo* obj_symbol = symbol_table.lookup(obj.name);
            if (obj_symbol && (obj_symbol->symbol_type == "variable" ||
                               obj_symbol->symbol_type == "parameter")) {
                const std::string obj_type = obj_symbol->data_type;

                if (obj_type == "string" && method_name == "length") {
                    if (!node.arguments.empty()) {
                        add_error("string.length() expects no arguments" +
                                  get_position_info(node.line, node.column));
                    }
                    return "int";
                }
                if (obj_type == "array") {
                    if (method_name == "length") {
                        if (!node.arguments.empty()) {
                            add_error("array.length() expects no arguments" +
                                      get_position_info(node.line, node.column));
                        }
                        return "int";
                    }
                    if (method_name == "append") {
                        if (node.arguments.size() != 1) {
                            add_error("array.append() expects exactly 1 argument" +
                                      get_position_info(node.line, node.column));
                        }
                        return "void";
                    }
                    if (method_name == "pop") {
                        if (!node.arguments.empty()) {
                            add_error("array.pop() expects no arguments" +
                                      get_position_info(node.line, node.column));
                        }
                        return "any";
                    }
                }
                if (obj_type == "dict" || obj_type.rfind("dict<", 0) == 0) {
                    if (method_name == "keys" || method_name == "values") {
                        return "array";
                    }
                }

                const auto class_it = classes.find(obj_type);
                if (class_it != classes.end()) {
                    const auto method_it = class_it->second.methods.find(method_name);
                    if (method_it != class_it->second.methods.end()) {
                        validate_call_arguments(method_name, node.arguments,
                                                method_it->second.params, true);
                        return method_it->second.return_type.empty()
                                   ? "any"
                                   : method_it->second.return_type;
                    }
                }
            }
        }

        if (member.object && is_node<MemberExpression>(member.object)) {
            const std::string obj_type = analyze(member.object);
            if (obj_type == "array") {
                if (method_name == "length") {
                    if (!node.arguments.empty()) {
                        add_error("array.length() expects no arguments" +
                                  get_position_info(node.line, node.column));
                    }
                    return "int";
                }
                if (method_name == "append") {
                    if (node.arguments.size() != 1) {
                        add_error("array.append() expects exactly 1 argument" +
                                  get_position_info(node.line, node.column));
                    }
                    return "void";
                }
                if (method_name == "pop") {
                    if (!node.arguments.empty()) {
                        add_error("array.pop() expects no arguments" +
                                  get_position_info(node.line, node.column));
                    }
                    return "any";
                }
            }
            if (obj_type == "string" && method_name == "length") {
                if (!node.arguments.empty()) {
                    add_error("string.length() expects no arguments" +
                              get_position_info(node.line, node.column));
                }
                return "int";
            }
            if ((obj_type == "dict" || obj_type.rfind("dict<", 0) == 0) &&
                (method_name == "keys" || method_name == "values")) {
                return "array";
            }

            const auto class_it = classes.find(obj_type);
            if (class_it != classes.end()) {
                const auto method_it = class_it->second.methods.find(method_name);
                if (method_it != class_it->second.methods.end()) {
                    return method_it->second.return_type.empty()
                               ? "any"
                               : method_it->second.return_type;
                }
            }
        }
    }

    return "unknown";
}

std::string SemanticAnalyzer::analyze_number_literal(NumberLiteral&) {
    return "int";
}

std::string SemanticAnalyzer::analyze_float_literal(FloatLiteral&) {
    return "float";
}

std::string SemanticAnalyzer::analyze_string_literal(StringLiteral&) {
    return "string";
}

std::string SemanticAnalyzer::analyze_array_literal(ArrayLiteral& node) {
    if (node.elements.empty()) {
        return "array";
    }

    const std::string first_type = analyze(node.elements[0]);
    for (std::size_t i = 1; i < node.elements.size(); ++i) {
        const std::string current_type = analyze(node.elements[i]);
        if (current_type != first_type) {
            add_error("Array elements must all be the same type. Expected " + first_type +
                      ", got " + current_type);
        }
    }

    return "array";
}

std::string SemanticAnalyzer::analyze_dict_literal(DictLiteral& node) {
    std::string key_type = node.key_type.empty() ? "any" : node.key_type;
    std::string value_type = node.value_type.empty() ? "any" : node.value_type;

    if (node.key_type.empty() && !node.keys.empty()) {
        key_type = analyze(node.keys[0]);
    }
    if (node.value_type.empty() && !node.values.empty()) {
        value_type = analyze(node.values[0]);
    }

    if (!node.keys.empty()) {
        for (std::size_t i = 0; i < node.keys.size() && i < node.values.size(); ++i) {
            const std::string key_result = analyze(node.keys[i]);
            const std::string value_result = analyze(node.values[i]);

            if (!node.key_type.empty() && key_result != node.key_type) {
                add_error("Dict key type mismatch. Expected " + node.key_type +
                          ", got " + key_result);
            }

            if (!node.value_type.empty() && value_result != node.value_type) {
                add_error("Dict value type mismatch. Expected " + node.value_type +
                          ", got " + value_result);
            }
        }
    }

    if (key_type == "any" && value_type == "any") {
        return "dict";
    }
    return "dict<" + key_type + ", " + value_type + ">";
}

std::string SemanticAnalyzer::analyze_bool_literal(BoolLiteral&) {
    return "bool";
}

std::string SemanticAnalyzer::analyze_null_literal(NullLiteral&) {
    return "null";
}

std::string SemanticAnalyzer::analyze_hasattr_expression(HasattrExpression& node) {
    const std::string obj_type = analyze(node.obj);

    const auto class_it = classes.find(obj_type);
    if (class_it != classes.end()) {
        bool has_attr = false;
        for (const auto& field : class_it->second.fields) {
            if (field.name == node.attr_name) {
                has_attr = true;
                break;
            }
        }
        if (!has_attr) {
            has_attr = class_it->second.methods.find(node.attr_name) != class_it->second.methods.end();
        }
        node.compile_time_result = has_attr;
    } else {
        node.compile_time_result = false;
    }

    return "bool";
}

std::string SemanticAnalyzer::analyze_while_statement(WhileStatement& node) {
    analyze(node.condition);
    if (node.then_body) {
        analyze(node.then_body);
    }
    return "";
}

std::string SemanticAnalyzer::analyze_for_in_statement(ForInStatement& node) {
    analyze(node.iterable);

    symbol_table.enter_scope();
    symbol_table.declare(node.variable, "variable", "any");
    if (node.body) {
        analyze(node.body);
    }
    symbol_table.exit_scope();

    return "";
}

std::string SemanticAnalyzer::analyze_set_statement(SetStatement& node) {
    const std::string target_type = analyze(node.target);
    const std::string value_type = analyze(node.value);

    if (!target_type.empty() && !value_type.empty() &&
        target_type != value_type && value_type != "any" && target_type != "any" &&
        value_type != "null") {
        add_error("Type mismatch" + get_position_info(node.line, node.column) +
                  ". Cannot assign type " + value_type + " to " + target_type);
    }

    return "";
}

std::string SemanticAnalyzer::analyze_return_statement(ReturnStatement& node) {
    if (node.value) {
        return analyze(node.value);
    }
    return "void";
}

std::string SemanticAnalyzer::analyze_block(Block& node) {
    for (auto& statement : node.statements) {
        analyze(statement);
    }
    return "";
}

std::string SemanticAnalyzer::analyze_try_statement(TryStatement& node) {
    symbol_table.enter_scope();
    analyze(node.try_block);
    symbol_table.exit_scope();

    std::vector<std::string> catch_types_seen;
    for (auto& catch_block : node.catch_blocks) {
        analyze_catch_block(catch_block, &catch_types_seen);
    }

    return "";
}

std::string SemanticAnalyzer::analyze_catch_block(CatchBlock& node,
                                                   std::vector<std::string>* catch_types_seen) {
    if (!node.exception_type.empty()) {
        const SymbolInfo* exc_class = symbol_table.lookup(node.exception_type);
        if (!exc_class || exc_class->symbol_type != "class") {
            add_error("Unknown exception type '" + node.exception_type + "'" +
                      get_position_info(node.line, node.column));
            return "";
        }

        if (catch_types_seen != nullptr) {
            if (std::find(catch_types_seen->begin(), catch_types_seen->end(),
                          node.exception_type) != catch_types_seen->end()) {
                add_error("Duplicate catch block for exception type '" + node.exception_type + "'" +
                          get_position_info(node.line, node.column));
            }
            catch_types_seen->push_back(node.exception_type);
        }
    }

    symbol_table.enter_scope();
    if (!node.identifier.empty()) {
        const std::string exc_type = node.exception_type.empty() ? "Exception" : node.exception_type;
        symbol_table.declare(node.identifier, "variable", exc_type);
    }

    analyze(node.body);
    symbol_table.exit_scope();

    return "";
}

std::string SemanticAnalyzer::analyze_throw_statement(ThrowStatement& node) {
    if (!node.expression) {
        return "";
    }

    const std::string expr_type = analyze(node.expression);
    if (expr_type.empty() || expr_type == "unknown") {
        add_error("Cannot determine type of expression being thrown" +
                  get_position_info(node.line, node.column));
        return "";
    }

    if (expr_type != "Exception") {
        const auto class_it = classes.find(expr_type);
        if (class_it == classes.end()) {
            add_error("Cannot throw type '" + expr_type + "' - must be an Exception" +
                      get_position_info(node.line, node.column));
            return "";
        }

        std::string parent = class_it->second.parent_class;
        bool extends_exception = false;
        while (!parent.empty()) {
            if (parent == "Exception") {
                extends_exception = true;
                break;
            }
            const auto parent_it = classes.find(parent);
            if (parent_it == classes.end()) {
                break;
            }
            parent = parent_it->second.parent_class;
        }

        if (!extends_exception) {
            add_error("Cannot throw type '" + expr_type + "' - must extend Exception" +
                      get_position_info(node.line, node.column));
        }
    }

    return "";
}
