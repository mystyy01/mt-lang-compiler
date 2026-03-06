#include "codegen.hpp"
#include "libc_functions.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

bool is_pointer_type(const std::string& type) {
    return !type.empty() && type.back() == '*';
}

std::string join_params(const std::vector<std::string>& params) {
    std::ostringstream out;
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << params[i];
    }
    return out.str();
}

std::string ast_variant_name(const ASTNode& node) {
    if (!node) {
        return "null";
    }
    switch (node->index()) {
        case 0: return "NumberLiteral";
        case 1: return "FloatLiteral";
        case 2: return "StringLiteral";
        case 3: return "BoolLiteral";
        case 4: return "NullLiteral";
        case 5: return "TypeLiteral";
        case 6: return "Identifier";
        case 7: return "ThisExpression";
        case 8: return "BinaryExpression";
        case 9: return "InExpression";
        case 10: return "ArrayLiteral";
        case 11: return "DictLiteral";
        case 12: return "VariableDeclaration";
        case 13: return "SetStatement";
        case 14: return "BreakStatement";
        case 15: return "ReturnStatement";
        case 16: return "Block";
        case 17: return "ExpressionStatement";
        case 18: return "CallExpression";
        case 19: return "MemberExpression";
        case 20: return "IndexExpression";
        case 21: return "TypeofExpression";
        case 22: return "HasattrExpression";
        case 23: return "ClassofExpression";
        case 24: return "IfStatement";
        case 25: return "WhileStatement";
        case 26: return "ForInStatement";
        case 27: return "FunctionDeclaration";
        case 28: return "ExternalDeclaration";
        case 29: return "DynamicFunctionDeclaration";
        case 30: return "FromImportStatement";
        case 31: return "SimpleImportStatement";
        case 32: return "LibcImportStatement";
        case 33: return "ClassDeclaration";
        case 34: return "NewExpression";
        case 35: return "MethodCallExpression";
        case 36: return "FieldAccessExpression";
        case 37: return "TryStatement";
        case 38: return "ThrowStatement";
        case 39: return "Program";
        default: return "UnknownNode";
    }
}

std::string map_libc_type_to_llvm(const std::string& ty) {
    if (ty == "int") {
        return "i32";
    }
    if (ty == "float") {
        return "double";
    }
    if (ty == "void") {
        return "void";
    }
    if (ty == "ptr") {
        return "i8*";
    }
    return "i8*";
}

void collect_called_functions(const ASTNode& node, std::unordered_set<std::string>* out) {
    if (!node || out == nullptr) {
        return;
    }

    if (is_node<CallExpression>(node)) {
        const auto& call = get_node<CallExpression>(node);
        if (call.callee && is_node<Identifier>(call.callee)) {
            out->insert(get_node<Identifier>(call.callee).name);
        } else if (call.callee && is_node<MemberExpression>(call.callee)) {
            const auto& member = get_node<MemberExpression>(call.callee);
            if (member.object && is_node<Identifier>(member.object)) {
                out->insert(member.property);
            }
        }
        collect_called_functions(call.callee, out);
        for (const auto& arg : call.arguments) {
            collect_called_functions(arg, out);
        }
        return;
    }
    if (is_node<BinaryExpression>(node)) {
        const auto& expr = get_node<BinaryExpression>(node);
        collect_called_functions(expr.left, out);
        collect_called_functions(expr.right, out);
        return;
    }
    if (is_node<InExpression>(node)) {
        const auto& expr = get_node<InExpression>(node);
        collect_called_functions(expr.item, out);
        collect_called_functions(expr.container, out);
        return;
    }
    if (is_node<ArrayLiteral>(node)) {
        const auto& lit = get_node<ArrayLiteral>(node);
        for (const auto& elem : lit.elements) {
            collect_called_functions(elem, out);
        }
        return;
    }
    if (is_node<DictLiteral>(node)) {
        const auto& lit = get_node<DictLiteral>(node);
        for (const auto& key : lit.keys) {
            collect_called_functions(key, out);
        }
        for (const auto& value : lit.values) {
            collect_called_functions(value, out);
        }
        return;
    }
    if (is_node<VariableDeclaration>(node)) {
        collect_called_functions(get_node<VariableDeclaration>(node).value, out);
        return;
    }
    if (is_node<SetStatement>(node)) {
        const auto& stmt = get_node<SetStatement>(node);
        collect_called_functions(stmt.target, out);
        collect_called_functions(stmt.value, out);
        return;
    }
    if (is_node<ExpressionStatement>(node)) {
        collect_called_functions(get_node<ExpressionStatement>(node).expression, out);
        return;
    }
    if (is_node<ReturnStatement>(node)) {
        collect_called_functions(get_node<ReturnStatement>(node).value, out);
        return;
    }
    if (is_node<Block>(node)) {
        const auto& block = get_node<Block>(node);
        for (const auto& stmt : block.statements) {
            collect_called_functions(stmt, out);
        }
        return;
    }
    if (is_node<IfStatement>(node)) {
        const auto& stmt = get_node<IfStatement>(node);
        collect_called_functions(stmt.condition, out);
        collect_called_functions(stmt.then_body, out);
        collect_called_functions(stmt.else_body, out);
        return;
    }
    if (is_node<WhileStatement>(node)) {
        const auto& stmt = get_node<WhileStatement>(node);
        collect_called_functions(stmt.condition, out);
        collect_called_functions(stmt.then_body, out);
        return;
    }
    if (is_node<ForInStatement>(node)) {
        const auto& stmt = get_node<ForInStatement>(node);
        collect_called_functions(stmt.iterable, out);
        collect_called_functions(stmt.body, out);
        return;
    }
    if (is_node<TypeofExpression>(node)) {
        collect_called_functions(get_node<TypeofExpression>(node).argument, out);
        return;
    }
    if (is_node<HasattrExpression>(node)) {
        collect_called_functions(get_node<HasattrExpression>(node).obj, out);
        return;
    }
    if (is_node<ClassofExpression>(node)) {
        collect_called_functions(get_node<ClassofExpression>(node).argument, out);
        return;
    }
    if (is_node<IndexExpression>(node)) {
        const auto& expr = get_node<IndexExpression>(node);
        collect_called_functions(expr.object, out);
        collect_called_functions(expr.index, out);
        return;
    }
    if (is_node<MemberExpression>(node)) {
        collect_called_functions(get_node<MemberExpression>(node).object, out);
        return;
    }
    if (is_node<FunctionDeclaration>(node)) {
        collect_called_functions(get_node<FunctionDeclaration>(node).body, out);
        return;
    }
    if (is_node<DynamicFunctionDeclaration>(node)) {
        collect_called_functions(get_node<DynamicFunctionDeclaration>(node).body, out);
        return;
    }
    if (is_node<ClassDeclaration>(node)) {
        const auto& class_decl = get_node<ClassDeclaration>(node);
        for (const auto& field : class_decl.fields) {
            collect_called_functions(field.initializer, out);
        }
        for (const auto& method : class_decl.methods) {
            collect_called_functions(method.body, out);
        }
        return;
    }
    if (is_node<NewExpression>(node)) {
        const auto& expr = get_node<NewExpression>(node);
        for (const auto& arg : expr.arguments) {
            collect_called_functions(arg, out);
        }
        return;
    }
    if (is_node<TryStatement>(node)) {
        const auto& stmt = get_node<TryStatement>(node);
        collect_called_functions(stmt.try_block, out);
        for (const auto& catch_block : stmt.catch_blocks) {
            collect_called_functions(catch_block.body, out);
        }
        return;
    }
    if (is_node<ThrowStatement>(node)) {
        collect_called_functions(get_node<ThrowStatement>(node).expression, out);
        return;
    }
}

bool contains_call_or_new(const ASTNode& node) {
    if (!node) {
        return false;
    }
    if (is_node<NumberLiteral>(node) || is_node<FloatLiteral>(node) ||
        is_node<StringLiteral>(node) || is_node<BoolLiteral>(node) ||
        is_node<NullLiteral>(node) || is_node<TypeLiteral>(node)) {
        return false;
    }
    if (is_node<CallExpression>(node) || is_node<NewExpression>(node)) {
        return true;
    }
    if (is_node<BinaryExpression>(node)) {
        return true;
    }
    if (is_node<InExpression>(node)) {
        return true;
    }
    if (is_node<ArrayLiteral>(node)) {
        const auto& n = get_node<ArrayLiteral>(node);
        for (const auto& elem : n.elements) {
            if (contains_call_or_new(elem)) {
                return true;
            }
        }
        return false;
    }
    if (is_node<DictLiteral>(node)) {
        const auto& n = get_node<DictLiteral>(node);
        for (const auto& k : n.keys) {
            if (contains_call_or_new(k)) {
                return true;
            }
        }
        for (const auto& v : n.values) {
            if (contains_call_or_new(v)) {
                return true;
            }
        }
        return false;
    }
    if (is_node<MemberExpression>(node)) {
        return true;
    }
    if (is_node<IndexExpression>(node)) {
        return true;
    }
    if (is_node<TypeofExpression>(node)) {
        return true;
    }
    if (is_node<HasattrExpression>(node)) {
        return true;
    }
    if (is_node<ClassofExpression>(node)) {
        return true;
    }
    if (is_node<Identifier>(node) || is_node<ThisExpression>(node)) {
        return true;
    }
    return true;
}

std::string infer_literal_mt_type(const ASTNode& node) {
    if (!node) {
        return "";
    }
    if (is_node<NumberLiteral>(node)) {
        return "int";
    }
    if (is_node<FloatLiteral>(node)) {
        return "float";
    }
    if (is_node<StringLiteral>(node)) {
        return "string";
    }
    if (is_node<BoolLiteral>(node)) {
        return "bool";
    }
    if (is_node<NullLiteral>(node)) {
        return "any";
    }
    return "any";
}

}  // namespace

CodeGenerator::CodeGenerator(bool emit_main_function, bool emit_builtin_decls)
    : current_block_terminated(false),
      entry_alloca_insert_index(0),
      register_counter(0),
      label_counter(0),
      string_counter(0),
      emit_main_function_flag(emit_main_function),
      emit_builtin_decls_flag(emit_builtin_decls) {}

std::string CodeGenerator::generate(ASTNode& root) {
    functions.clear();
    classes.clear();
    class_type_tags.clear();
    string_constants.clear();
    top_level_variables.clear();
    module_globals.clear();
    global_lines.clear();
    declaration_lines.clear();
    function_lines.clear();
    variable_scopes.clear();
    break_labels.clear();
    try_contexts.clear();
    current_function_name.clear();
    current_return_type.clear();
    entry_alloca_insert_index = 0;
    register_counter = 0;
    label_counter = 0;
    string_counter = 0;
    current_block_terminated = false;

    if (emit_builtin_decls_flag) {
        for (const auto& [symbol, libc_info] : LIBC_FUNCTIONS) {
            CodegenFunctionInfo info;
            info.name = symbol;
            info.return_type = map_libc_type_to_llvm(libc_info.ret);
            info.return_mt_type = libc_info.ret;
            info.is_external = true;
            info.is_var_arg = libc_info.var_arg;
            for (const std::string& arg_ty : libc_info.args) {
                info.parameters.emplace_back("", map_libc_type_to_llvm(arg_ty), nullptr, arg_ty);
            }
            functions[symbol] = std::move(info);
        }
    }

    if (!is_node<Program>(root)) {
        throw std::runtime_error("CodeGenerator expects a Program node");
    }

    Program& program = get_node<Program>(root);

    collect_program_declarations(program);
    emit_prelude();
    if (emit_builtin_decls_flag) {
        emit_builtin_declarations();
    }
    emit_user_declarations();

    std::unordered_set<std::string> reachable_functions;
    if (emit_main_function_flag) {
        std::vector<std::string> worklist;
        std::size_t work_index = 0;

        auto enqueue_call = [&](const std::string& name) {
            const auto it = functions.find(name);
            if (it == functions.end() || it->second.is_external) {
                return;
            }
            worklist.push_back(name);
        };

        std::unordered_set<std::string> discovered_calls;
        for (const auto& statement : program.statements) {
            if (!statement || is_node<FunctionDeclaration>(statement) ||
                is_node<DynamicFunctionDeclaration>(statement) ||
                is_node<ExternalDeclaration>(statement) ||
                is_node<ClassDeclaration>(statement) ||
                is_node<FromImportStatement>(statement) ||
                is_node<SimpleImportStatement>(statement) ||
                is_node<LibcImportStatement>(statement)) {
                continue;
            }
            collect_called_functions(statement, &discovered_calls);
        }

        for (const auto& statement : program.statements) {
            if (!statement || !is_node<ClassDeclaration>(statement)) {
                continue;
            }
            const auto& class_decl = get_node<ClassDeclaration>(statement);
            for (const auto& method : class_decl.methods) {
                collect_called_functions(method.body, &discovered_calls);
            }
        }

        for (const auto& name : discovered_calls) {
            enqueue_call(name);
        }

        while (work_index < worklist.size()) {
            const std::string name = worklist[work_index++];
            if (reachable_functions.count(name) > 0) {
                continue;
            }
            reachable_functions.insert(name);

            for (const auto& statement : program.statements) {
                if (!statement) {
                    continue;
                }
                if (is_node<FunctionDeclaration>(statement)) {
                    const auto& fn = get_node<FunctionDeclaration>(statement);
                    if (fn.name == name) {
                        std::unordered_set<std::string> nested_calls;
                        collect_called_functions(fn.body, &nested_calls);
                        for (const auto& nested : nested_calls) {
                            enqueue_call(nested);
                        }
                        break;
                    }
                } else if (is_node<DynamicFunctionDeclaration>(statement)) {
                    const auto& fn = get_node<DynamicFunctionDeclaration>(statement);
                    if (fn.name == name) {
                        std::unordered_set<std::string> nested_calls;
                        collect_called_functions(fn.body, &nested_calls);
                        for (const auto& nested : nested_calls) {
                            enqueue_call(nested);
                        }
                        break;
                    }
                }
            }
        }
    }

    for (auto& statement : program.statements) {
        if (statement && is_node<FunctionDeclaration>(statement)) {
            FunctionDeclaration& fn = get_node<FunctionDeclaration>(statement);
            if (!emit_main_function_flag || reachable_functions.count(fn.name) > 0) {
                emit_function_definition(fn);
            }
        } else if (statement && is_node<DynamicFunctionDeclaration>(statement)) {
            DynamicFunctionDeclaration& fn = get_node<DynamicFunctionDeclaration>(statement);
            if (!emit_main_function_flag || reachable_functions.count(fn.name) > 0) {
                emit_dynamic_function_definition(fn);
            }
        } else if (statement && is_node<ClassDeclaration>(statement)) {
            const ClassDeclaration& class_decl = get_node<ClassDeclaration>(statement);
            const auto class_it = classes.find(class_decl.name);
            if (class_it == classes.end()) {
                throw std::runtime_error("Internal error: missing class declaration for '" +
                                         class_decl.name + "'");
            }
            for (const auto& [method_name, method_info] : class_it->second.methods) {
                (void)method_name;
                if (method_info.ast_node == nullptr) {
                    continue;
                }
                emit_class_method_definition(method_info);
            }
        }
    }

    if (emit_main_function_flag) {
        emit_main_function(program);
    }

    std::ostringstream out;
    for (const auto& line : global_lines) {
        out << line << '\n';
    }
    for (const auto& line : declaration_lines) {
        out << line << '\n';
    }
    out << '\n';
    for (const auto& line : function_lines) {
        out << line << '\n';
    }

    return out.str();
}

void CodeGenerator::collect_program_declarations(Program& program) {
    for (auto& statement : program.statements) {
        if (!statement) {
            continue;
        }
        if (is_node<FunctionDeclaration>(statement)) {
            register_function_declaration(get_node<FunctionDeclaration>(statement));
        } else if (is_node<DynamicFunctionDeclaration>(statement)) {
            register_dynamic_function_declaration(get_node<DynamicFunctionDeclaration>(statement));
        } else if (is_node<ExternalDeclaration>(statement)) {
            register_external_declaration(get_node<ExternalDeclaration>(statement));
        } else if (is_node<LibcImportStatement>(statement)) {
            register_libc_import_declaration(get_node<LibcImportStatement>(statement));
        } else if (is_node<ClassDeclaration>(statement)) {
            register_class_declaration(get_node<ClassDeclaration>(statement));
        } else if (is_node<VariableDeclaration>(statement)) {
            auto& decl = get_node<VariableDeclaration>(statement);
            top_level_variables.push_back(clone_node(statement));

            // Fixed-size arrays use stack-allocated [N x T], can't be i8* globals
            if (decl.fixed_size > 0) {
                continue;
            }

            const std::string llvm_type = map_type_to_llvm(decl.type);
            if (llvm_type == "void") {
                continue;
            }
            if (module_globals.find(decl.name) != module_globals.end()) {
                continue;
            }

            const std::string symbol = "@__mt_global_" + decl.name;
            std::string class_name;
            if (!is_builtin_mt_type(decl.type) && classes.find(decl.type) != classes.end()) {
                class_name = decl.type;
            }

            // Build metadata for arrays and dicts so functions see the full type info
            bool is_dyn_array = (decl.type == "array");
            std::string dyn_elem_llvm;
            std::string dyn_elem_mt;
            if (is_dyn_array) {
                dyn_elem_mt = decl.element_type.empty() ? "int" : decl.element_type;
                dyn_elem_llvm = map_type_to_llvm(dyn_elem_mt);
                if (dyn_elem_llvm == "void") {
                    dyn_elem_llvm = "i8*";
                    dyn_elem_mt = "any";
                }
            }

            bool is_dict = (decl.type == "dict");
            std::string dict_key_llvm, dict_val_llvm, dict_key_mt, dict_val_mt;
            if (is_dict) {
                dict_key_mt = decl.key_type.empty() ? "any" : decl.key_type;
                dict_val_mt = decl.value_type.empty() ? "any" : decl.value_type;
                dict_key_llvm = map_type_to_llvm(dict_key_mt);
                dict_val_llvm = map_type_to_llvm(dict_val_mt);
            }

            module_globals.emplace(decl.name, VariableInfo{
                                            llvm_type,
                                            symbol,
                                            false,
                                            "",
                                            0,
                                            is_dyn_array,
                                            dyn_elem_llvm,
                                            class_name,
                                            is_dict,
                                            dict_key_llvm,
                                            dict_val_llvm,
                                            dict_key_mt,
                                            dict_val_mt,
                                            dyn_elem_mt,
                                        });
        }
    }

    std::vector<std::string> class_names;
    class_names.reserve(classes.size());
    for (const auto& [class_name, _] : classes) {
        (void)_;
        class_names.push_back(class_name);
    }
    std::sort(class_names.begin(), class_names.end());

    int next_tag = 1;
    for (const auto& class_name : class_names) {
        class_type_tags[class_name] = next_tag;
        auto it = classes.find(class_name);
        if (it != classes.end()) {
            it->second.type_tag = next_tag;
        }
        ++next_tag;
    }
}

void CodeGenerator::register_function_declaration(FunctionDeclaration& node) {
    CodegenFunctionInfo info;
    info.name = node.name;
    info.return_type = map_type_to_llvm(node.return_type);
    info.return_mt_type = node.return_type;
    info.is_external = false;

    for (const auto& param : node.parameters) {
        info.parameters.emplace_back(
            param.name,
            map_type_to_llvm(param.param_type.empty() ? "int" : param.param_type),
            clone_node(param.default_value),
            param.param_type.empty() ? "int" : param.param_type);
    }

    functions[node.name] = std::move(info);
}

void CodeGenerator::register_dynamic_function_declaration(DynamicFunctionDeclaration& node) {
    CodegenFunctionInfo info;
    info.name = node.name;
    info.return_type = "i8*";
    info.return_mt_type = "any";
    info.is_external = false;

    for (const auto& param : node.parameters) {
        info.parameters.emplace_back(
            param.name,
            map_type_to_llvm(param.param_type.empty() ? "any" : param.param_type),
            clone_node(param.default_value),
            param.param_type.empty() ? "any" : param.param_type);
    }

    functions[node.name] = std::move(info);
}

void CodeGenerator::register_external_declaration(ExternalDeclaration& node) {
    CodegenFunctionInfo info;
    info.name = node.name;
    info.return_type = map_type_to_llvm(node.return_type);
    info.return_mt_type = node.return_type;
    info.is_external = true;
    info.is_var_arg = false;

    for (const auto& param : node.parameters) {
        info.parameters.emplace_back(
            param.name,
            map_type_to_llvm(param.param_type.empty() ? "int" : param.param_type),
            clone_node(param.default_value),
            param.param_type.empty() ? "int" : param.param_type);
    }

    functions[node.name] = std::move(info);
}

void CodeGenerator::register_libc_import_declaration(LibcImportStatement& node) {
    for (const std::string& symbol : node.symbols) {
        if (functions.find(symbol) != functions.end()) {
            continue;
        }

        const auto libc_it = LIBC_FUNCTIONS.find(symbol);
        if (libc_it == LIBC_FUNCTIONS.end()) {
            continue;
        }

        CodegenFunctionInfo info;
        info.name = symbol;
        info.return_type = map_libc_type_to_llvm(libc_it->second.ret);
        info.return_mt_type = libc_it->second.ret;
        info.is_external = true;
        info.is_var_arg = libc_it->second.var_arg;

        for (const std::string& arg_ty : libc_it->second.args) {
            info.parameters.emplace_back("", map_libc_type_to_llvm(arg_ty), nullptr, arg_ty);
        }

        functions[symbol] = std::move(info);
    }
}

void CodeGenerator::register_class_declaration(ClassDeclaration& node) {
    CodegenClassInfo class_info;
    class_info.name = node.name;
    class_info.parent_class = node.inherits_from;

    std::size_t offset = 0;
    std::size_t max_alignment = 1;

    if (!node.inherits_from.empty()) {
        const auto parent_it = classes.find(node.inherits_from);
        if (parent_it == classes.end()) {
            throw std::runtime_error("Unknown parent class '" + node.inherits_from +
                                     "' for class '" + node.name + "' in codegen");
        }

        for (const auto& [field_name, parent_field] : parent_it->second.fields) {
            CodegenClassFieldInfo inherited_field;
            inherited_field.name = parent_field.name;
            inherited_field.mt_type = parent_field.mt_type;
            inherited_field.element_mt_type = parent_field.element_mt_type;
            inherited_field.llvm_type = parent_field.llvm_type;
            inherited_field.offset = parent_field.offset;
            inherited_field.is_constructor_arg = parent_field.is_constructor_arg;
            inherited_field.initializer = clone_node(parent_field.initializer);
            class_info.fields[field_name] = std::move(inherited_field);
        }
        class_info.methods = parent_it->second.methods;

        // Inherited methods are emitted with the class that declares them.
        for (auto& [method_name, method_info] : class_info.methods) {
            (void)method_name;
            method_info.ast_node = nullptr;
        }

        offset = parent_it->second.object_size;
        for (const auto& [field_name, field_info] : class_info.fields) {
            (void)field_name;
            const std::size_t size = llvm_type_size(field_info.llvm_type);
            const std::size_t alignment = std::max<std::size_t>(1, std::min<std::size_t>(size, 8));
            max_alignment = std::max(max_alignment, alignment);
            offset = std::max(offset, field_info.offset + size);
        }
    }

    for (const auto& field : node.fields) {
        if (class_info.fields.find(field.name) != class_info.fields.end()) {
            throw std::runtime_error("Field '" + field.name + "' in class '" + node.name +
                                     "' conflicts with inherited field");
        }

        CodegenClassFieldInfo field_info;
        field_info.name = field.name;
        field_info.mt_type = field.type;
        field_info.element_mt_type = field.element_type;
        field_info.llvm_type = map_type_to_llvm(field.type);
        field_info.initializer = clone_node(field.initializer);

        const std::size_t size = llvm_type_size(field_info.llvm_type);
        const std::size_t alignment = std::max<std::size_t>(1, std::min<std::size_t>(size, 8));
        offset = align_up(offset, alignment);
        field_info.offset = offset;
        offset += size;
        max_alignment = std::max(max_alignment, alignment);

        class_info.fields[field_info.name] = std::move(field_info);
    }

    class_info.object_size = offset == 0 ? 1 : align_up(offset, max_alignment);

    for (const auto& method : node.methods) {
        CodegenClassMethodInfo method_info;
        method_info.class_name = node.name;
        method_info.method_name = method.name;
        method_info.mangled_name = node.name + "__" + method.name;
        method_info.return_type = map_type_to_llvm(method.return_type.empty() ? "any" : method.return_type);
        method_info.return_mt_type = method.return_type.empty() ? "any" : method.return_type;
        method_info.ast_node = &method;

        method_info.parameters.emplace_back("this", "i8*", nullptr, node.name);
        for (const auto& param : method.params) {
            method_info.parameters.emplace_back(
                param.name,
                map_type_to_llvm(param.param_type.empty() ? "any" : param.param_type),
                clone_node(param.default_value),
                param.param_type.empty() ? "any" : param.param_type);
        }

        class_info.methods[method.name] = std::move(method_info);
    }

    classes[node.name] = std::move(class_info);
}

void CodeGenerator::emit_prelude() {
    global_lines.push_back("; ModuleID = 'mt_lang'");
    global_lines.push_back("source_filename = \"mt_lang\"");
    global_lines.push_back("@__mt_runtime_abi_version = constant i32 1");
    global_lines.push_back("@__mt_char_pool = internal global [8388608 x i8] zeroinitializer");
    global_lines.push_back("@__mt_char_pool_index = internal global i64 0");
    global_lines.push_back("@__mt_exc_jmp = internal global i8* null");
    global_lines.push_back("@__mt_exc_obj = internal global i8* null");
    global_lines.push_back("@__mt_exc_tag = internal global i32 0");
    global_lines.push_back("@__mt_argc = internal global i32 0");
    global_lines.push_back("@__mt_argv = internal global i8** null");
    for (const auto& [_, info] : module_globals) {
        (void)_;
        std::string zero_init = "0";
        if (info.llvm_type == "double") {
            zero_init = "0.0";
        } else if (info.llvm_type == "i1") {
            zero_init = "0";
        } else if (is_pointer_type(info.llvm_type)) {
            zero_init = "null";
        }
        global_lines.push_back(info.ptr_value + " = internal global " + info.llvm_type + " " + zero_init);
    }
    global_lines.push_back("");

    declaration_lines.push_back("@stdin = external global i8*");
    declaration_lines.push_back("declare i32 @setjmp(i8*)");
    declaration_lines.push_back("declare void @longjmp(i8*, i32)");
    declaration_lines.push_back("declare i64 @strtol(i8*, i8**, i32)");
    declaration_lines.push_back("declare i32 @printf(i8*, ...)");
    declaration_lines.push_back("declare void @exit(i32)");

    function_lines.push_back("define i8* @__mt_char(i8 %c) {");
    function_lines.push_back("entry:");
    function_lines.push_back("  %idx = load i64, i64* @__mt_char_pool_index");
    function_lines.push_back("  %slot = urem i64 %idx, 4194304");
    function_lines.push_back("  %offset = mul i64 %slot, 2");
    function_lines.push_back("  %base = getelementptr inbounds [8388608 x i8], [8388608 x i8]* @__mt_char_pool, i64 0, i64 0");
    function_lines.push_back("  %ptr = getelementptr inbounds i8, i8* %base, i64 %offset");
    function_lines.push_back("  store i8 %c, i8* %ptr");
    function_lines.push_back("  %nul_ptr = getelementptr inbounds i8, i8* %ptr, i64 1");
    function_lines.push_back("  store i8 0, i8* %nul_ptr");
    function_lines.push_back("  %next = add i64 %idx, 1");
    function_lines.push_back("  store i64 %next, i64* @__mt_char_pool_index");
    function_lines.push_back("  ret i8* %ptr");
    function_lines.push_back("}");
    function_lines.push_back("");

    StringConstantInfo panic_fmt = get_or_create_string_constant("[mt-runtime] fatal(%d): %s\n");
    function_lines.push_back("define void @__mt_runtime_panic(i8* %msg, i32 %code) {");
    function_lines.push_back("entry:");
    function_lines.push_back(
        "  %panic_fmt = getelementptr inbounds [" + std::to_string(panic_fmt.length) +
        " x i8], [" + std::to_string(panic_fmt.length) + " x i8]* " + panic_fmt.symbol +
        ", i32 0, i32 0");
    function_lines.push_back(
        "  %panic_print = call i32 (i8*, ...) @printf(i8* %panic_fmt, i32 %code, i8* %msg)");
    function_lines.push_back("  call void @exit(i32 %code)");
    function_lines.push_back("  unreachable");
    function_lines.push_back("}");
    function_lines.push_back("");
}

void CodeGenerator::emit_builtin_declarations() {
    declaration_lines.push_back("declare i8* @malloc(i64)");
    declaration_lines.push_back("declare i8* @realloc(i8*, i64)");
    declaration_lines.push_back("declare i64 @strlen(i8*)");
    declaration_lines.push_back("declare i8* @strcpy(i8*, i8*)");
    declaration_lines.push_back("declare i8* @strcat(i8*, i8*)");
    declaration_lines.push_back("declare i32 @sprintf(i8*, i8*, ...)");
    declaration_lines.push_back("declare i32 @atoi(i8*)");
    declaration_lines.push_back("declare double @atof(i8*)");
    declaration_lines.push_back("declare i32 @strcmp(i8*, i8*)");
    declaration_lines.push_back("declare i8* @strstr(i8*, i8*)");
}

void CodeGenerator::emit_user_declarations() {
    static const std::unordered_set<std::string> kBuiltinDeclared = {
        "printf", "exit", "malloc", "realloc", "strlen", "strcpy", "strcat", "sprintf", "atoi", "atof",
        "strcmp", "strstr",
    };

    for (const auto& [name, info] : functions) {
        if (!info.is_external) {
            continue;
        }
        if (emit_builtin_decls_flag && kBuiltinDeclared.count(name)) {
            continue;
        }

        std::vector<std::string> params;
        params.reserve(info.parameters.size());
        for (const auto& param : info.parameters) {
            params.push_back(param.llvm_type);
        }

        std::string signature = join_params(params);
        if (info.is_var_arg) {
            if (!signature.empty()) {
                signature += ", ...";
            } else {
                signature = "...";
            }
        }

        declaration_lines.push_back(
            "declare " + info.return_type + " @" + name + "(" + signature + ")");
    }
}

void CodeGenerator::begin_function(const CodegenFunctionInfo& info) {
    current_function_name = info.name;
    current_return_type = info.return_type;
    current_block_terminated = false;
    variable_scopes.clear();
    break_labels.clear();
    try_contexts.clear();

    std::vector<std::string> params;
    params.reserve(info.parameters.size());
    for (const auto& param : info.parameters) {
        params.push_back(param.llvm_type + " %" + param.name);
    }

    function_lines.push_back("define " + info.return_type + " @" + info.name + "(" +
                             join_params(params) + ") {");
    emit_label("entry");
    entry_alloca_insert_index = function_lines.size();

    push_scope();
    for (const auto& [name, var] : module_globals) {
        declare_variable(name, var);
    }
    for (const auto& param : info.parameters) {
        const std::string ptr = next_register(param.name + "_addr");
        emit_line(ptr + " = alloca " + param.llvm_type);
        emit_line("store " + param.llvm_type + " %" + param.name + ", " +
                  param.llvm_type + "* " + ptr);
        std::string class_name;
        if (!param.mt_type.empty() && classes.find(param.mt_type) != classes.end()) {
            class_name = param.mt_type;
        }
        bool is_dynamic_array = (param.mt_type == "array");
        std::string dynamic_array_elem_mt_type;
        std::string dynamic_array_elem_llvm_type;
        if (is_dynamic_array) {
            dynamic_array_elem_mt_type = "int";
            dynamic_array_elem_llvm_type = map_type_to_llvm(dynamic_array_elem_mt_type);
            if (dynamic_array_elem_llvm_type == "void") {
                dynamic_array_elem_mt_type = "any";
                dynamic_array_elem_llvm_type = "i8*";
            }
        }

        const bool is_dict = (param.mt_type == "dict" || param.mt_type.rfind("dict<", 0) == 0);
        std::string dict_key_mt_type;
        std::string dict_value_mt_type;
        std::string dict_key_llvm_type;
        std::string dict_value_llvm_type;
        if (is_dict) {
            // Function parameters currently don't carry explicit dict<K,V> metadata in the AST,
            // so treat them as dict<any, any> at codegen time.
            dict_key_mt_type = "any";
            dict_value_mt_type = "any";
            dict_key_llvm_type = "i8*";
            dict_value_llvm_type = "i8*";
        }
        declare_variable(param.name, VariableInfo{
            param.llvm_type,
            ptr,
            false,
            "",
            0,
            is_dynamic_array,
            dynamic_array_elem_llvm_type,
            class_name,
            is_dict,
            dict_key_llvm_type,
            dict_value_llvm_type,
            dict_key_mt_type,
            dict_value_mt_type,
            dynamic_array_elem_mt_type,
        });
    }
}

void CodeGenerator::end_function() {
    if (!current_block_terminated) {
        if (current_return_type == "void") {
            emit_line("ret void");
        } else if (current_return_type == "i1") {
            emit_line("ret i1 0");
        } else if (current_return_type == "double") {
            emit_line("ret double 0.0");
        } else if (is_pointer_type(current_return_type)) {
            emit_line("ret " + current_return_type + " null");
        } else {
            emit_line("ret " + current_return_type + " 0");
        }
        current_block_terminated = true;
    }

    function_lines.push_back("}");
    function_lines.push_back("");

    current_function_name.clear();
    current_return_type.clear();
    current_block_terminated = false;
    entry_alloca_insert_index = 0;
    variable_scopes.clear();
    break_labels.clear();
    try_contexts.clear();
}

void CodeGenerator::emit_function_definition(FunctionDeclaration& node) {
    const auto it = functions.find(node.name);
    if (it == functions.end()) {
        throw std::runtime_error("Internal error: missing function declaration for '" + node.name + "'");
    }

    begin_function(it->second);
    if (!variable_scopes.empty()) {
        auto& scope = variable_scopes.back();
        for (const auto& param : node.parameters) {
            auto var_it = scope.find(param.name);
            if (var_it == scope.end()) {
                continue;
            }
            if (param.param_type == "array") {
                var_it->second.is_dynamic_array = true;
                var_it->second.dynamic_array_elem_mt_type =
                    param.element_type.empty() ? "int" : param.element_type;
                var_it->second.dynamic_array_elem_llvm_type =
                    map_type_to_llvm(var_it->second.dynamic_array_elem_mt_type);
            }
        }
    }

    if (!node.body || !is_node<Block>(node.body)) {
        throw std::runtime_error("Function '" + node.name + "' has invalid body");
    }

    auto& body = get_node<Block>(node.body);
    generate_block(body);

    end_function();
}

void CodeGenerator::emit_dynamic_function_definition(DynamicFunctionDeclaration& node) {
    const auto it = functions.find(node.name);
    if (it == functions.end()) {
        throw std::runtime_error("Internal error: missing dynamic function declaration for '" + node.name + "'");
    }

    begin_function(it->second);
    if (!variable_scopes.empty()) {
        auto& scope = variable_scopes.back();
        for (const auto& param : node.parameters) {
            auto var_it = scope.find(param.name);
            if (var_it == scope.end()) {
                continue;
            }
            if (param.param_type == "array") {
                var_it->second.is_dynamic_array = true;
                var_it->second.dynamic_array_elem_mt_type =
                    param.element_type.empty() ? "int" : param.element_type;
                var_it->second.dynamic_array_elem_llvm_type =
                    map_type_to_llvm(var_it->second.dynamic_array_elem_mt_type);
            }
        }
    }

    if (!node.body || !is_node<Block>(node.body)) {
        throw std::runtime_error("Dynamic function '" + node.name + "' has invalid body");
    }

    auto& body = get_node<Block>(node.body);
    generate_block(body);

    end_function();
}

void CodeGenerator::emit_class_method_definition(const CodegenClassMethodInfo& method_info) {
    if (method_info.ast_node == nullptr) {
        throw std::runtime_error("Class method '" + method_info.method_name + "' has invalid AST body");
    }

    CodegenFunctionInfo fn_info;
    fn_info.name = method_info.mangled_name;
    fn_info.return_type = method_info.return_type;
    fn_info.parameters = method_info.parameters;
    fn_info.is_external = false;

    begin_function(fn_info);
    if (method_info.ast_node != nullptr && !variable_scopes.empty()) {
        auto& scope = variable_scopes.back();
        for (const auto& param : method_info.ast_node->params) {
            auto var_it = scope.find(param.name);
            if (var_it == scope.end()) {
                continue;
            }
            if (param.param_type == "array") {
                var_it->second.is_dynamic_array = true;
                var_it->second.dynamic_array_elem_mt_type =
                    param.element_type.empty() ? "int" : param.element_type;
                var_it->second.dynamic_array_elem_llvm_type =
                    map_type_to_llvm(var_it->second.dynamic_array_elem_mt_type);
            }
        }
    }

    if (!method_info.ast_node->body || !is_node<Block>(method_info.ast_node->body)) {
        throw std::runtime_error("Class method '" + method_info.class_name + "." +
                                 method_info.method_name + "' has invalid body");
    }

    ASTNode body_node = clone_node(method_info.ast_node->body);
    if (!body_node || !is_node<Block>(body_node)) {
        throw std::runtime_error("Class method '" + method_info.class_name + "." +
                                 method_info.method_name + "' body clone failed");
    }
    auto& body = get_node<Block>(body_node);
    generate_block(body);

    end_function();
}

void CodeGenerator::emit_main_function(Program& program) {
    CodegenFunctionInfo main_info;
    main_info.name = "main";
    main_info.return_type = "i32";
    main_info.parameters.push_back(CodegenParameterInfo("argc", "i32"));
    main_info.parameters.push_back(CodegenParameterInfo("argv", "i8**"));

    begin_function(main_info);

    emit_line("store i32 %argc, i32* @__mt_argc");
    emit_line("store i8** %argv, i8*** @__mt_argv");

    for (auto& statement : program.statements) {
        if (!statement) {
            continue;
        }
        if (is_node<FunctionDeclaration>(statement) ||
            is_node<DynamicFunctionDeclaration>(statement) ||
            is_node<ExternalDeclaration>(statement) ||
            is_node<ClassDeclaration>(statement) ||
            is_node<FromImportStatement>(statement) ||
            is_node<SimpleImportStatement>(statement) ||
            is_node<LibcImportStatement>(statement)) {
            continue;
        }

        generate_statement(statement);
    }

    end_function();
}

void CodeGenerator::emit_line(const std::string& line) {
    if (line.find(" = alloca ") != std::string::npos &&
        !current_function_name.empty() &&
        entry_alloca_insert_index <= function_lines.size()) {
        function_lines.insert(function_lines.begin() + static_cast<std::ptrdiff_t>(entry_alloca_insert_index),
                              "  " + line);
        ++entry_alloca_insert_index;
        return;
    }

    function_lines.push_back("  " + line);
    if (line.rfind("ret ", 0) == 0 || line.rfind("br ", 0) == 0 || line.rfind("unreachable", 0) == 0) {
        current_block_terminated = true;
    }
}

void CodeGenerator::emit_label(const std::string& label) {
    function_lines.push_back(label + ":");
    current_block_terminated = false;
}

std::string CodeGenerator::next_register(const std::string& prefix) {
    return "%" + prefix + "." + std::to_string(register_counter++);
}

std::string CodeGenerator::next_label(const std::string& prefix) {
    return prefix + "." + std::to_string(label_counter++);
}

void CodeGenerator::push_scope() {
    variable_scopes.emplace_back();
}

void CodeGenerator::pop_scope() {
    if (!variable_scopes.empty()) {
        variable_scopes.pop_back();
    }
}

void CodeGenerator::declare_variable(const std::string& name, const VariableInfo& info) {
    if (variable_scopes.empty()) {
        push_scope();
    }
    variable_scopes.back()[name] = info;
}

const CodeGenerator::VariableInfo* CodeGenerator::lookup_variable(const std::string& name) const {
    for (auto it = variable_scopes.rbegin(); it != variable_scopes.rend(); ++it) {
        const auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

bool CodeGenerator::materialize_top_level_variable(const std::string& name) {
    if (name.empty() || lookup_variable(name) != nullptr) {
        return false;
    }

    for (const auto& stmt : top_level_variables) {
        if (!stmt || !is_node<VariableDeclaration>(stmt)) {
            continue;
        }
        const auto& decl = get_node<VariableDeclaration>(stmt);
        if (decl.name != name) {
            continue;
        }
        if (contains_call_or_new(decl.value)) {
            return false;
        }

        ASTNode cloned = clone_node(stmt);
        if (!cloned || !is_node<VariableDeclaration>(cloned)) {
            return false;
        }
        generate_variable_declaration(get_node<VariableDeclaration>(cloned));
        return true;
    }

    return false;
}

const CodeGenerator::VariableInfo* CodeGenerator::resolve_variable(const std::string& name) {
    const VariableInfo* var = lookup_variable(name);
    if (var) {
        return var;
    }
    if (materialize_top_level_variable(name)) {
        return lookup_variable(name);
    }
    return nullptr;
}

bool CodeGenerator::is_top_level_variable_name(const std::string& name) const {
    if (name.empty()) {
        return false;
    }
    for (const auto& stmt : top_level_variables) {
        if (!stmt || !is_node<VariableDeclaration>(stmt)) {
            continue;
        }
        const auto& decl = get_node<VariableDeclaration>(stmt);
        if (decl.name == name) {
            return true;
        }
    }
    return false;
}

bool CodeGenerator::is_builtin_mt_type(const std::string& mt_type) const {
    return mt_type == "int" || mt_type == "float" || mt_type == "bool" || mt_type == "string" ||
           mt_type == "void" || mt_type == "array" || mt_type == "dict" || mt_type == "any" ||
           mt_type.rfind("dict<", 0) == 0;
}

std::string CodeGenerator::map_type_to_llvm(const std::string& mt_type) const {
    if (mt_type.empty()) {
        return "i32";
    }
    if (mt_type == "int") {
        return "i32";
    }
    if (mt_type == "float") {
        return "double";
    }
    if (mt_type == "bool") {
        return "i1";
    }
    if (mt_type == "string") {
        return "i8*";
    }
    if (mt_type == "void") {
        return "void";
    }

    if (mt_type == "array" || mt_type == "dict" || mt_type == "any" || mt_type.rfind("dict<", 0) == 0) {
        return "i8*";
    }

    if (classes.find(mt_type) != classes.end()) {
        return "i8*";
    }

    return "i8*";
}

CodeGenerator::IRValue CodeGenerator::generate_expression(ASTNode& node) {
    if (!node) {
        return IRValue{"void", "", false};
    }

    if (is_node<NumberLiteral>(node)) {
        return generate_number_literal(get_node<NumberLiteral>(node));
    }
    if (is_node<FloatLiteral>(node)) {
        return generate_float_literal(get_node<FloatLiteral>(node));
    }
    if (is_node<StringLiteral>(node)) {
        return generate_string_literal(get_node<StringLiteral>(node));
    }
    if (is_node<BoolLiteral>(node)) {
        return generate_bool_literal(get_node<BoolLiteral>(node));
    }
    if (is_node<ArrayLiteral>(node)) {
        return generate_array_literal(get_node<ArrayLiteral>(node));
    }
    if (is_node<DictLiteral>(node)) {
        return generate_dict_literal(get_node<DictLiteral>(node));
    }
    if (is_node<NullLiteral>(node)) {
        return generate_null_literal();
    }
    if (is_node<Identifier>(node)) {
        return generate_identifier(get_node<Identifier>(node));
    }
    if (is_node<ThisExpression>(node)) {
        return generate_this_expression(get_node<ThisExpression>(node));
    }
    if (is_node<BinaryExpression>(node)) {
        return generate_binary_expression(get_node<BinaryExpression>(node));
    }
    if (is_node<InExpression>(node)) {
        return generate_in_expression(get_node<InExpression>(node));
    }
    if (is_node<CallExpression>(node)) {
        return generate_call_expression(get_node<CallExpression>(node));
    }
    if (is_node<TypeofExpression>(node)) {
        return generate_typeof_expression(get_node<TypeofExpression>(node));
    }
    if (is_node<HasattrExpression>(node)) {
        return generate_hasattr_expression(get_node<HasattrExpression>(node));
    }
    if (is_node<IndexExpression>(node)) {
        return generate_index_expression(get_node<IndexExpression>(node));
    }
    if (is_node<MemberExpression>(node)) {
        return generate_member_expression(get_node<MemberExpression>(node));
    }
    if (is_node<NewExpression>(node)) {
        return generate_new_expression(get_node<NewExpression>(node));
    }
    if (is_node<ClassofExpression>(node)) {
        auto& expr = get_node<ClassofExpression>(node);
        IRValue arg = generate_expression(expr.argument);
        std::string class_name = infer_class_name_from_ast(expr.argument);
        if (!class_name.empty()) {
            StringConstantInfo info = get_or_create_string_constant(class_name);
            return IRValue{"i8*", string_constant_gep(info), true};
        }

        std::string type_name = "unknown";
        if (arg.type == "i32") {
            type_name = "int";
        } else if (arg.type == "double") {
            type_name = "float";
        } else if (arg.type == "i1") {
            type_name = "bool";
        } else if (arg.type == "i8*") {
            type_name = "string";
        }
        StringConstantInfo info = get_or_create_string_constant(type_name);
        return IRValue{"i8*", string_constant_gep(info), true};
    }

    throw std::runtime_error("Unsupported expression node in codegen: " + ast_variant_name(node));
}

void CodeGenerator::generate_statement(ASTNode& node) {
    if (!node || current_block_terminated) {
        return;
    }

    if (is_node<VariableDeclaration>(node)) {
        generate_variable_declaration(get_node<VariableDeclaration>(node));
        return;
    }
    if (is_node<SetStatement>(node)) {
        generate_set_statement(get_node<SetStatement>(node));
        return;
    }
    if (is_node<ExpressionStatement>(node)) {
        generate_expression_statement(get_node<ExpressionStatement>(node));
        return;
    }
    if (is_node<ReturnStatement>(node)) {
        generate_return_statement(get_node<ReturnStatement>(node));
        return;
    }
    if (is_node<Block>(node)) {
        generate_block(get_node<Block>(node));
        return;
    }
    if (is_node<IfStatement>(node)) {
        generate_if_statement(get_node<IfStatement>(node));
        return;
    }
    if (is_node<WhileStatement>(node)) {
        generate_while_statement(get_node<WhileStatement>(node));
        return;
    }
    if (is_node<ForInStatement>(node)) {
        ForInStatement& for_node = get_node<ForInStatement>(node);
        if (for_node.iterable && is_node<CallExpression>(for_node.iterable)) {
            CallExpression& call = get_node<CallExpression>(for_node.iterable);
            const bool is_range_call =
                call.callee && is_node<Identifier>(call.callee) &&
                get_node<Identifier>(call.callee).name == "range" && call.arguments.size() == 1;
            if (is_range_call) {
                const std::string cond_label = next_label("for_cond");
                const std::string body_label = next_label("for_body");
                const std::string end_label = next_label("for_end");

                const std::string loop_ptr = next_register(for_node.variable + "_addr");
                emit_line(loop_ptr + " = alloca i32");
                emit_line("store i32 0, i32* " + loop_ptr);

                push_scope();
                declare_variable(for_node.variable, VariableInfo{
                    "i32",
                    loop_ptr,
                    false,
                    "",
                    0,
                    false,
                    "",
                    "",
                    false,
                    "",
                    "",
                    "",
                    "",
                });

                emit_line("br label %" + cond_label);
                emit_label(cond_label);

                IRValue end_value = cast_value(generate_expression(call.arguments[0]), "i32");
                const std::string loop_value = next_register(for_node.variable + "_val");
                emit_line(loop_value + " = load i32, i32* " + loop_ptr);
                const std::string cond_reg = next_register("for_cmp");
                emit_line(cond_reg + " = icmp slt i32 " + loop_value + ", " + end_value.value);
                emit_line("br i1 " + cond_reg + ", label %" + body_label + ", label %" + end_label);

                emit_label(body_label);
                break_labels.push_back(end_label);
                if (for_node.body && is_node<Block>(for_node.body)) {
                    generate_block(get_node<Block>(for_node.body));
                }
                break_labels.pop_back();

                if (!current_block_terminated) {
                    const std::string cur = next_register("for_i");
                    emit_line(cur + " = load i32, i32* " + loop_ptr);
                    const std::string inc = next_register("for_next");
                    emit_line(inc + " = add i32 " + cur + ", 1");
                    emit_line("store i32 " + inc + ", i32* " + loop_ptr);
                    emit_line("br label %" + cond_label);
                }

                emit_label(end_label);
                pop_scope();
                return;
            }
        }

        std::string iterable_name = "iterable";
        const VariableInfo* iterable_var = nullptr;
        VariableInfo iterable_temp;

        if (for_node.iterable && is_node<Identifier>(for_node.iterable)) {
            iterable_name = get_node<Identifier>(for_node.iterable).name;
            iterable_var = resolve_variable(iterable_name);
        }

        if (!(iterable_var && (iterable_var->is_fixed_array || iterable_var->is_dynamic_array))) {
            IRValue iterable_value = cast_value(generate_expression(for_node.iterable), "i8*");
            const std::string iterable_ptr = next_register("for_iterable_addr");
            emit_line(iterable_ptr + " = alloca i8*");
            emit_line("store i8* " + iterable_value.value + ", i8** " + iterable_ptr);
            iterable_temp = VariableInfo{
                "i8*",
                iterable_ptr,
                false,
                "",
                0,
                true,
                "i8*",
                "",
                false,
                "",
                "",
                "",
                "",
            };
            iterable_var = &iterable_temp;
        }

        const bool is_fixed = iterable_var->is_fixed_array;
        const std::string elem_type = is_fixed
            ? iterable_var->fixed_array_elem_llvm_type
            : iterable_var->dynamic_array_elem_llvm_type;

        const std::string idx_ptr = next_register(for_node.variable + "_idx");
        emit_line(idx_ptr + " = alloca i64");
        emit_line("store i64 0, i64* " + idx_ptr);

        const std::string loop_var_ptr = next_register(for_node.variable + "_addr");
        emit_line(loop_var_ptr + " = alloca " + elem_type);

        const std::string cond_label = next_label("for_arr_cond");
        const std::string body_label = next_label("for_arr_body");
        const std::string end_label = next_label("for_arr_end");

        // Propagate class_name if the array element type is a known class
        std::string loop_var_class_name;
        const std::string& elem_mt_type = iterable_var->dynamic_array_elem_mt_type;
        if (!elem_mt_type.empty() && classes.find(elem_mt_type) != classes.end()) {
            loop_var_class_name = elem_mt_type;
        }

        push_scope();
        declare_variable(for_node.variable, VariableInfo{
            elem_type,
            loop_var_ptr,
            false,
            "",
            0,
            false,
            "",
            loop_var_class_name,
            false,
            "",
            "",
            "",
            "",
        });

        emit_line("br label %" + cond_label);
        emit_label(cond_label);

        const std::string idx = next_register("for_arr_idx");
        emit_line(idx + " = load i64, i64* " + idx_ptr);

        std::string len_value;
        if (is_fixed) {
            len_value = std::to_string(iterable_var->fixed_array_size);
        } else {
            const std::string header = next_register(iterable_name + "_hdr");
            emit_line(header + " = load i8*, i8** " + iterable_var->ptr_value);
            const std::string len_slot = next_register(iterable_name + "_len_slot");
            emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
            const std::string dyn_len = next_register(iterable_name + "_len");
            emit_line(dyn_len + " = load i64, i64* " + len_slot);
            len_value = dyn_len;
        }

        const std::string cond = next_register("for_arr_cmp");
        emit_line(cond + " = icmp slt i64 " + idx + ", " + len_value);
        emit_line("br i1 " + cond + ", label %" + body_label + ", label %" + end_label);

        emit_label(body_label);

        if (is_fixed) {
            const std::string array_type = "[" + std::to_string(iterable_var->fixed_array_size) + " x " +
                                           iterable_var->fixed_array_elem_llvm_type + "]";
            const std::string elem_ptr = next_register(iterable_name + "_iter_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + array_type + ", " + array_type + "* " +
                      iterable_var->ptr_value + ", i64 0, i64 " + idx);
            const std::string elem_val = next_register(iterable_name + "_iter_val");
            emit_line(elem_val + " = load " + elem_type + ", " + elem_type + "* " + elem_ptr);
            emit_line("store " + elem_type + " " + elem_val + ", " + elem_type + "* " + loop_var_ptr);
        } else {
            const std::string header = next_register(iterable_name + "_iter_hdr");
            emit_line(header + " = load i8*, i8** " + iterable_var->ptr_value);
            const std::string data_slot_raw = next_register(iterable_name + "_iter_data_slot_raw");
            emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 16");
            const std::string data_slot = next_register(iterable_name + "_iter_data_slot");
            emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");
            const std::string data_raw = next_register(iterable_name + "_iter_data_raw");
            emit_line(data_raw + " = load i8*, i8** " + data_slot);
            const std::string typed_data = next_register(iterable_name + "_iter_data");
            emit_line(typed_data + " = bitcast i8* " + data_raw + " to " + elem_type + "*");
            const std::string elem_ptr = next_register(iterable_name + "_iter_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + elem_type + ", " + elem_type + "* " +
                      typed_data + ", i64 " + idx);
            const std::string elem_val = next_register(iterable_name + "_iter_val");
            emit_line(elem_val + " = load " + elem_type + ", " + elem_type + "* " + elem_ptr);
            emit_line("store " + elem_type + " " + elem_val + ", " + elem_type + "* " + loop_var_ptr);
        }

        break_labels.push_back(end_label);
        if (for_node.body && is_node<Block>(for_node.body)) {
            generate_block(get_node<Block>(for_node.body));
        }
        break_labels.pop_back();

        if (!current_block_terminated) {
            const std::string cur = next_register("for_arr_i");
            emit_line(cur + " = load i64, i64* " + idx_ptr);
            const std::string inc = next_register("for_arr_next");
            emit_line(inc + " = add i64 " + cur + ", 1");
            emit_line("store i64 " + inc + ", i64* " + idx_ptr);
            emit_line("br label %" + cond_label);
        }

        emit_label(end_label);
        pop_scope();
        return;
    }
    if (is_node<BreakStatement>(node)) {
        generate_break_statement(get_node<BreakStatement>(node));
        return;
    }

    if (is_node<TryStatement>(node)) {
        auto& try_stmt = get_node<TryStatement>(node);
        const std::string setjmp_label = next_label("try_setjmp");
        const std::string try_body_label = next_label("try_body");
        const std::string catch_dispatch_label = next_label("try_catch");
        const std::string end_label = next_label("try_end");

        const std::string jmp_buf_addr = next_register("try_jmp_buf");
        emit_line(jmp_buf_addr + " = alloca [256 x i8]");
        const std::string jmp_ptr = next_register("try_jmp_ptr");
        emit_line(jmp_ptr + " = getelementptr inbounds [256 x i8], [256 x i8]* " + jmp_buf_addr +
                  ", i64 0, i64 0");

        const std::string prev_jmp_addr = next_register("try_prev_jmp_addr");
        emit_line(prev_jmp_addr + " = alloca i8*");
        const std::string prev_jmp = next_register("try_prev_jmp");
        emit_line(prev_jmp + " = load i8*, i8** @__mt_exc_jmp");
        emit_line("store i8* " + prev_jmp + ", i8** " + prev_jmp_addr);
        emit_line("store i8* " + jmp_ptr + ", i8** @__mt_exc_jmp");

        emit_line("br label %" + setjmp_label);
        emit_label(setjmp_label);
        const std::string setjmp_result = next_register("setjmp_result");
        emit_line(setjmp_result + " = call i32 @setjmp(i8* " + jmp_ptr + ")");
        const std::string is_try_path = next_register("try_is_initial");
        emit_line(is_try_path + " = icmp eq i32 " + setjmp_result + ", 0");
        emit_line("br i1 " + is_try_path + ", label %" + try_body_label + ", label %" +
                  catch_dispatch_label);

        try_contexts.push_back(TryContext{prev_jmp_addr});

        emit_label(try_body_label);
        if (try_stmt.try_block && is_node<Block>(try_stmt.try_block)) {
            generate_block(get_node<Block>(try_stmt.try_block));
        }
        if (!current_block_terminated) {
            const std::string restored_prev = next_register("try_restore_prev");
            emit_line(restored_prev + " = load i8*, i8** " + prev_jmp_addr);
            emit_line("store i8* " + restored_prev + ", i8** @__mt_exc_jmp");
            emit_line("br label %" + end_label);
        }

        emit_label(catch_dispatch_label);
        {
            const std::string restored_prev = next_register("catch_restore_prev");
            emit_line(restored_prev + " = load i8*, i8** " + prev_jmp_addr);
            emit_line("store i8* " + restored_prev + ", i8** @__mt_exc_jmp");
        }

        const std::string thrown_tag = next_register("caught_tag");
        emit_line(thrown_tag + " = load i32, i32* @__mt_exc_tag");
        const std::string thrown_obj = next_register("caught_obj");
        emit_line(thrown_obj + " = load i8*, i8** @__mt_exc_obj");

        bool has_catch_all = false;
        for (std::size_t i = 0; i < try_stmt.catch_blocks.size(); ++i) {
            auto& catch_block = try_stmt.catch_blocks[i];
            const bool is_catch_all = catch_block.exception_type.empty();
            const std::string catch_body_label = next_label("catch_body");
            std::string catch_next_label;

            if (is_catch_all) {
                emit_line("br label %" + catch_body_label);
                has_catch_all = true;
            } else {
                catch_next_label = next_label("catch_next");

                std::vector<int> matching_tags;
                for (const auto& [class_name, tag] : class_type_tags) {
                    if (class_is_a(class_name, catch_block.exception_type)) {
                        matching_tags.push_back(tag);
                    }
                }
                std::sort(matching_tags.begin(), matching_tags.end());
                matching_tags.erase(std::unique(matching_tags.begin(), matching_tags.end()),
                                    matching_tags.end());

                std::string match_value = "0";
                if (!matching_tags.empty()) {
                    std::string accumulated_cmp;
                    for (std::size_t tag_index = 0; tag_index < matching_tags.size(); ++tag_index) {
                        const std::string cmp = next_register("catch_match");
                        emit_line(cmp + " = icmp eq i32 " + thrown_tag + ", " +
                                  std::to_string(matching_tags[tag_index]));
                        if (tag_index == 0) {
                            accumulated_cmp = cmp;
                        } else {
                            const std::string or_value = next_register("catch_match_or");
                            emit_line(or_value + " = or i1 " + accumulated_cmp + ", " + cmp);
                            accumulated_cmp = or_value;
                        }
                    }
                    match_value = accumulated_cmp;
                }

                emit_line("br i1 " + match_value + ", label %" + catch_body_label +
                          ", label %" + catch_next_label);
            }

            emit_label(catch_body_label);
            push_scope();
            if (!catch_block.identifier.empty()) {
                const std::string catch_type_name =
                    catch_block.exception_type.empty() ? "Exception" : catch_block.exception_type;
                const std::string catch_llvm_type = map_type_to_llvm(catch_type_name);
                const std::string catch_var_addr = next_register(catch_block.identifier + "_addr");
                emit_line(catch_var_addr + " = alloca " + catch_llvm_type);

                std::string catch_obj_value = thrown_obj;
                if (catch_llvm_type != "i8*") {
                    const std::string cast_obj = next_register("caught_obj_cast");
                    emit_line(cast_obj + " = bitcast i8* " + thrown_obj + " to " + catch_llvm_type);
                    catch_obj_value = cast_obj;
                }
                emit_line("store " + catch_llvm_type + " " + catch_obj_value + ", " +
                          catch_llvm_type + "* " + catch_var_addr);

                std::string catch_class_name;
                if (!catch_type_name.empty() && classes.find(catch_type_name) != classes.end()) {
                    catch_class_name = catch_type_name;
                }
                declare_variable(catch_block.identifier, VariableInfo{
                    catch_llvm_type,
                    catch_var_addr,
                    false,
                    "",
                    0,
                    false,
                    "",
                    catch_class_name,
                    false,
                    "",
                    "",
                    "",
                    "",
                });
            }

            emit_line("store i8* null, i8** @__mt_exc_obj");
            emit_line("store i32 0, i32* @__mt_exc_tag");

            if (catch_block.body && is_node<Block>(catch_block.body)) {
                generate_block(get_node<Block>(catch_block.body));
            }
            pop_scope();

            if (!current_block_terminated) {
                emit_line("br label %" + end_label);
            }

            if (is_catch_all) {
                break;
            }
            emit_label(catch_next_label);
        }

        if (!has_catch_all) {
            StringConstantInfo msg =
                get_or_create_string_constant("Unhandled exception (no matching catch)");
            IRValue msg_arg{"i8*", string_constant_gep(msg), true};
            IRValue code_arg{"i32", "1001", true};
            (void)emit_call("void", "@__mt_runtime_panic", {msg_arg, code_arg}, true);
            emit_line("unreachable");
        }

        emit_label(end_label);
        if (!try_contexts.empty()) {
            try_contexts.pop_back();
        }
        return;
    }
    if (is_node<ThrowStatement>(node)) {
        auto& throw_stmt = get_node<ThrowStatement>(node);
        IRValue thrown_value{"i8*", "null", true};
        int thrown_tag = 0;
        if (throw_stmt.expression) {
            thrown_value = cast_value(generate_expression(throw_stmt.expression), "i8*");
            const std::string thrown_class = infer_class_name_from_ast(throw_stmt.expression);
            thrown_tag = class_tag_for_name(thrown_class);
        }

        emit_line("store i8* " + thrown_value.value + ", i8** @__mt_exc_obj");
        emit_line("store i32 " + std::to_string(thrown_tag) + ", i32* @__mt_exc_tag");

        const std::string jmp_ptr = next_register("throw_jmp_ptr");
        emit_line(jmp_ptr + " = load i8*, i8** @__mt_exc_jmp");
        const std::string has_jmp = next_register("throw_has_jmp");
        emit_line(has_jmp + " = icmp ne i8* " + jmp_ptr + ", null");

        const std::string longjmp_label = next_label("throw_longjmp");
        const std::string exit_label = next_label("throw_exit");
        emit_line("br i1 " + has_jmp + ", label %" + longjmp_label + ", label %" + exit_label);

        emit_label(longjmp_label);
        emit_line("call void @longjmp(i8* " + jmp_ptr + ", i32 1)");
        emit_line("unreachable");

        emit_label(exit_label);
        StringConstantInfo unhandled = get_or_create_string_constant("Unhandled exception");
        IRValue msg{"i8*", string_constant_gep(unhandled), true};
        IRValue code{"i32", "1001", true};
        (void)emit_call("void", "@__mt_runtime_panic", {msg, code}, true);
        emit_line("unreachable");
        return;
    }

    if (is_node<ClassDeclaration>(node) || is_node<FromImportStatement>(node) ||
        is_node<SimpleImportStatement>(node) || is_node<LibcImportStatement>(node) ||
        is_node<ExternalDeclaration>(node) || is_node<FunctionDeclaration>(node) ||
        is_node<DynamicFunctionDeclaration>(node)) {
        return;
    }

    throw std::runtime_error("Unsupported statement node in codegen: " + ast_variant_name(node));
}

CodeGenerator::IRValue CodeGenerator::generate_number_literal(NumberLiteral& node) {
    return IRValue{"i32", std::to_string(node.value), true};
}

CodeGenerator::IRValue CodeGenerator::generate_float_literal(FloatLiteral& node) {
    std::ostringstream out;
    out << std::setprecision(17) << node.value;
    std::string value = out.str();
    if (value.find('.') == std::string::npos &&
        value.find('e') == std::string::npos &&
        value.find('E') == std::string::npos) {
        value += ".0";
    }
    return IRValue{"double", value, true};
}

CodeGenerator::IRValue CodeGenerator::generate_string_literal(StringLiteral& node) {
    StringConstantInfo info = get_or_create_string_constant(node.value);
    return IRValue{"i8*", string_constant_gep(info), true};
}

CodeGenerator::IRValue CodeGenerator::generate_bool_literal(BoolLiteral& node) {
    return IRValue{"i1", node.value ? "1" : "0", true};
}

CodeGenerator::IRValue CodeGenerator::generate_array_literal(
    ArrayLiteral& node,
    const std::string& forced_element_mt_type) {
    std::string element_type = forced_element_mt_type;
    if (element_type.empty()) {
        element_type = "int";
        if (!node.elements.empty()) {
            ASTNode& first = node.elements[0];
            if (is_node<FloatLiteral>(first)) {
                element_type = "float";
            } else if (is_node<StringLiteral>(first)) {
                element_type = "string";
            } else if (is_node<BoolLiteral>(first)) {
                element_type = "bool";
            } else if (is_node<NullLiteral>(first)) {
                element_type = "any";
            }
        }
    }

    const std::string elem_llvm_type = map_type_to_llvm(element_type);
    std::size_t elem_size = 8;
    if (elem_llvm_type == "i1") {
        elem_size = 1;
    } else if (elem_llvm_type == "i32") {
        elem_size = 4;
    } else if (elem_llvm_type == "double") {
        elem_size = 8;
    }

    const std::size_t initial_len = node.elements.size();
    const std::size_t capacity = std::max<std::size_t>(4, initial_len);
    const std::size_t total_bytes = capacity * elem_size;

    const std::string header_raw = next_register("arr_lit_hdr_raw");
    emit_line(header_raw + " = call i8* @malloc(i64 24)");

    const std::string len_slot = next_register("arr_lit_len_slot");
    emit_line(len_slot + " = bitcast i8* " + header_raw + " to i64*");
    emit_line("store i64 " + std::to_string(initial_len) + ", i64* " + len_slot);

    const std::string cap_slot = next_register("arr_lit_cap_slot");
    emit_line(cap_slot + " = getelementptr inbounds i64, i64* " + len_slot + ", i64 1");
    emit_line("store i64 " + std::to_string(capacity) + ", i64* " + cap_slot);

    const std::string data_slot_raw = next_register("arr_lit_data_slot_raw");
    emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header_raw + ", i64 16");
    const std::string data_slot = next_register("arr_lit_data_slot");
    emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");

    const std::string data_raw = next_register("arr_lit_data_raw");
    emit_line(data_raw + " = call i8* @malloc(i64 " + std::to_string(total_bytes) + ")");
    emit_line("store i8* " + data_raw + ", i8** " + data_slot);

    const std::string typed_data = next_register("arr_lit_typed_data");
    emit_line(typed_data + " = bitcast i8* " + data_raw + " to " + elem_llvm_type + "*");
    for (std::size_t i = 0; i < node.elements.size(); ++i) {
        IRValue value = cast_value(generate_expression(node.elements[i]), elem_llvm_type);
        const std::string elem_ptr = next_register("arr_lit_elem_ptr");
        emit_line(elem_ptr + " = getelementptr inbounds " + elem_llvm_type + ", " +
                  elem_llvm_type + "* " + typed_data + ", i64 " + std::to_string(i));
        emit_line("store " + elem_llvm_type + " " + value.value + ", " +
                  elem_llvm_type + "* " + elem_ptr);
    }

    return IRValue{"i8*", header_raw, true};
}

CodeGenerator::IRValue CodeGenerator::generate_dict_literal(DictLiteral& node) {
    std::string key_mt_type = node.key_type;
    std::string value_mt_type = node.value_type;
    if (key_mt_type.empty() && !node.keys.empty()) {
        key_mt_type = infer_literal_mt_type(node.keys[0]);
    }
    if (value_mt_type.empty() && !node.values.empty()) {
        value_mt_type = infer_literal_mt_type(node.values[0]);
    }
    if (key_mt_type.empty()) {
        key_mt_type = "any";
    }
    if (value_mt_type.empty()) {
        value_mt_type = "any";
    }

    std::string key_llvm_type = map_type_to_llvm(key_mt_type);
    std::string value_llvm_type = map_type_to_llvm(value_mt_type);
    if (key_llvm_type == "void") {
        key_llvm_type = "i8*";
        key_mt_type = "any";
    }
    if (value_llvm_type == "void") {
        value_llvm_type = "i8*";
        value_mt_type = "any";
    }

    const std::size_t pair_count = std::min(node.keys.size(), node.values.size());
    const std::size_t key_size = llvm_type_size(key_llvm_type);
    const std::size_t value_size = llvm_type_size(value_llvm_type);
    const std::size_t capacity = std::max<std::size_t>(4, pair_count);
    const std::size_t key_bytes = std::max<std::size_t>(1, capacity * key_size);
    const std::size_t value_bytes = std::max<std::size_t>(1, capacity * value_size);

    const std::string header_raw = next_register("dict_lit_hdr_raw");
    emit_line(header_raw + " = call i8* @malloc(i64 32)");

    const std::string len_slot = next_register("dict_lit_len_slot");
    emit_line(len_slot + " = bitcast i8* " + header_raw + " to i64*");
    emit_line("store i64 " + std::to_string(pair_count) + ", i64* " + len_slot);

    const std::string cap_slot = next_register("dict_lit_cap_slot");
    emit_line(cap_slot + " = getelementptr inbounds i64, i64* " + len_slot + ", i64 1");
    emit_line("store i64 " + std::to_string(capacity) + ", i64* " + cap_slot);

    const std::string keys_slot_raw = next_register("dict_lit_keys_slot_raw");
    emit_line(keys_slot_raw + " = getelementptr inbounds i8, i8* " + header_raw + ", i64 16");
    const std::string keys_slot = next_register("dict_lit_keys_slot");
    emit_line(keys_slot + " = bitcast i8* " + keys_slot_raw + " to i8**");

    const std::string values_slot_raw = next_register("dict_lit_values_slot_raw");
    emit_line(values_slot_raw + " = getelementptr inbounds i8, i8* " + header_raw + ", i64 24");
    const std::string values_slot = next_register("dict_lit_values_slot");
    emit_line(values_slot + " = bitcast i8* " + values_slot_raw + " to i8**");

    const std::string keys_raw = next_register("dict_lit_keys_raw");
    emit_line(keys_raw + " = call i8* @malloc(i64 " + std::to_string(key_bytes) + ")");
    emit_line("store i8* " + keys_raw + ", i8** " + keys_slot);

    const std::string values_raw = next_register("dict_lit_values_raw");
    emit_line(values_raw + " = call i8* @malloc(i64 " + std::to_string(value_bytes) + ")");
    emit_line("store i8* " + values_raw + ", i8** " + values_slot);

    const std::string keys_typed = next_register("dict_lit_keys_typed");
    emit_line(keys_typed + " = bitcast i8* " + keys_raw + " to " + key_llvm_type + "*");
    const std::string values_typed = next_register("dict_lit_values_typed");
    emit_line(values_typed + " = bitcast i8* " + values_raw + " to " + value_llvm_type + "*");

    for (std::size_t i = 0; i < pair_count; ++i) {
        IRValue key_value = cast_value(generate_expression(node.keys[i]), key_llvm_type);
        IRValue value_value = cast_value(generate_expression(node.values[i]), value_llvm_type);

        const std::string key_ptr = next_register("dict_lit_key_ptr");
        emit_line(key_ptr + " = getelementptr inbounds " + key_llvm_type + ", " + key_llvm_type +
                  "* " + keys_typed + ", i64 " + std::to_string(i));
        emit_line("store " + key_llvm_type + " " + key_value.value + ", " + key_llvm_type +
                  "* " + key_ptr);

        const std::string value_ptr = next_register("dict_lit_value_ptr");
        emit_line(value_ptr + " = getelementptr inbounds " + value_llvm_type + ", " +
                  value_llvm_type + "* " + values_typed + ", i64 " + std::to_string(i));
        emit_line("store " + value_llvm_type + " " + value_value.value + ", " +
                  value_llvm_type + "* " + value_ptr);
    }

    return IRValue{"i8*", header_raw, true};
}

CodeGenerator::IRValue CodeGenerator::generate_null_literal() {
    return IRValue{"i8*", "null", true};
}

CodeGenerator::IRValue CodeGenerator::generate_identifier(Identifier& node) {
    const VariableInfo* var = resolve_variable(node.name);
    if (!var) {
        throw std::runtime_error("Unknown variable '" + node.name + "' in codegen");
    }
    if (var->is_fixed_array) {
        throw std::runtime_error(
            "Fixed-size array '" + node.name +
            "' cannot be used directly as a scalar expression; index it with []");
    }

    const std::string reg = next_register(node.name + "_load");
    emit_line(reg + " = load " + var->llvm_type + ", " + var->llvm_type + "* " + var->ptr_value);
    return IRValue{var->llvm_type, reg, true};
}

CodeGenerator::IRValue CodeGenerator::generate_this_expression(ThisExpression&) {
    const VariableInfo* var = resolve_variable("this");
    if (!var) {
        throw std::runtime_error("'this' used outside class method");
    }
    const std::string reg = next_register("this_load");
    emit_line(reg + " = load " + var->llvm_type + ", " + var->llvm_type + "* " + var->ptr_value);
    return IRValue{var->llvm_type, reg, true};
}

CodeGenerator::IRValue CodeGenerator::generate_member_expression(MemberExpression& node) {
    if (node.property == "length") {
        throw std::runtime_error("Use length() or .length() call form for runtime length");
    }

    const VariableInfo* obj_var = nullptr;
    std::string object_name;
    if (node.object && is_node<Identifier>(node.object)) {
        object_name = get_node<Identifier>(node.object).name;
        obj_var = resolve_variable(object_name);
    } else if (node.object && is_node<ThisExpression>(node.object)) {
        object_name = "this";
        obj_var = resolve_variable("this");
    }
    if (!obj_var) {
        IRValue object_value = generate_expression(node.object);
        if (object_value.type == "double") {
            return IRValue{"double", "0.0", true};
        }
        if (object_value.type == "i1") {
            return IRValue{"i1", "0", true};
        }
        if (object_value.type == "i32" || object_value.type == "i64") {
            return IRValue{"i32", "0", true};
        }
        return IRValue{"i8*", "null", true};
    }
    if (obj_var->class_name.empty()) {
        return IRValue{"i8*", "null", true};
    }

    const auto class_it = classes.find(obj_var->class_name);
    if (class_it == classes.end()) {
        throw std::runtime_error("Unknown class type '" + obj_var->class_name + "'");
    }

    const auto field_it = class_it->second.fields.find(node.property);
    if (field_it == class_it->second.fields.end()) {
        throw std::runtime_error("Unknown field '" + node.property + "' for class '" +
                                 obj_var->class_name + "'");
    }

    const std::string obj_ptr = next_register(object_name + "_obj");
    emit_line(obj_ptr + " = load i8*, i8** " + obj_var->ptr_value);
    const std::string field_raw = next_register(object_name + "_field_raw");
    emit_line(field_raw + " = getelementptr inbounds i8, i8* " + obj_ptr + ", i64 " +
              std::to_string(field_it->second.offset));
    const std::string field_ptr = next_register(object_name + "_field_ptr");
    emit_line(field_ptr + " = bitcast i8* " + field_raw + " to " + field_it->second.llvm_type + "*");
    const std::string field_val = next_register(object_name + "_field_val");
    emit_line(field_val + " = load " + field_it->second.llvm_type + ", " +
              field_it->second.llvm_type + "* " + field_ptr);
    return IRValue{field_it->second.llvm_type, field_val, true};
}

CodeGenerator::IRValue CodeGenerator::generate_new_expression(NewExpression& node) {
    const auto class_it = classes.find(node.class_name);
    if (class_it == classes.end()) {
        throw std::runtime_error("Unknown class '" + node.class_name + "' in codegen");
    }

    const CodegenClassInfo& class_info = class_it->second;
    const std::string obj_ptr = next_register(node.class_name + "_obj");
    emit_line(obj_ptr + " = call i8* @malloc(i64 " + std::to_string(class_info.object_size) + ")");

    // Zero/default initialize fields first.
    for (const auto& [field_name, field] : class_info.fields) {
        (void)field_name;
        const std::string field_raw = next_register(node.class_name + "_init_field_raw");
        emit_line(field_raw + " = getelementptr inbounds i8, i8* " + obj_ptr + ", i64 " +
                  std::to_string(field.offset));
        const std::string field_ptr = next_register(node.class_name + "_init_field_ptr");
        emit_line(field_ptr + " = bitcast i8* " + field_raw + " to " + field.llvm_type + "*");
        std::string default_value = "0";
        if (field.llvm_type == "double") {
            default_value = "0.0";
        } else if (is_pointer_type(field.llvm_type)) {
            default_value = "null";
        }
        emit_line("store " + field.llvm_type + " " + default_value + ", " +
                  field.llvm_type + "* " + field_ptr);
    }

    // Then apply field initializers.
    for (const auto& [field_name, field] : class_info.fields) {
        if (!field.initializer) {
            continue;
        }
        ASTNode init_expr = clone_node(field.initializer);
        IRValue value;
        if (field.mt_type == "array" && init_expr && is_node<ArrayLiteral>(init_expr)) {
            std::string elem_mt_type = field.element_mt_type.empty() ? "any" : field.element_mt_type;
            value = cast_value(generate_array_literal(get_node<ArrayLiteral>(init_expr), elem_mt_type),
                               field.llvm_type);
        } else {
            value = cast_value(generate_expression(init_expr), field.llvm_type);
        }
        const std::string field_raw = next_register(node.class_name + "_field_init_raw");
        emit_line(field_raw + " = getelementptr inbounds i8, i8* " + obj_ptr + ", i64 " +
                  std::to_string(field.offset));
        const std::string field_ptr = next_register(node.class_name + "_field_init_ptr");
        emit_line(field_ptr + " = bitcast i8* " + field_raw + " to " + field.llvm_type + "*");
        emit_line("store " + field.llvm_type + " " + value.value + ", " +
                  field.llvm_type + "* " + field_ptr);
    }

    // If the class defines `new(...)`, call it automatically on instantiation.
    const auto init_it = class_info.methods.find("new");
    if (init_it != class_info.methods.end()) {
        const CodegenClassMethodInfo& init_method = init_it->second;
        const std::size_t expected_params = init_method.parameters.size() > 0
            ? init_method.parameters.size() - 1 : 0;

        std::vector<ASTNode> arg_nodes;
        arg_nodes.reserve(node.arguments.size());
        for (const auto& arg : node.arguments) {
            arg_nodes.push_back(clone_node(arg));
        }
        while (arg_nodes.size() < expected_params) {
            const std::size_t param_index = arg_nodes.size() + 1;
            if (param_index >= init_method.parameters.size() ||
                !init_method.parameters[param_index].default_value) {
                break;
            }
            arg_nodes.push_back(clone_node(init_method.parameters[param_index].default_value));
        }
        if (arg_nodes.size() != expected_params) {
            throw std::runtime_error("Constructor '" + node.class_name + ".new' argument count mismatch");
        }

        std::vector<IRValue> init_args;
        init_args.reserve(1 + arg_nodes.size());
        init_args.push_back(IRValue{"i8*", obj_ptr, true});

        for (std::size_t i = 0; i < arg_nodes.size(); ++i) {
            const auto& param = init_method.parameters[i + 1];
            IRValue arg = cast_value(generate_expression(arg_nodes[i]), param.llvm_type);
            init_args.push_back(std::move(arg));
        }

        (void)emit_call(init_method.return_type, "@" + init_method.mangled_name, init_args);
    } else if (!node.arguments.empty()) {
        throw std::runtime_error(
            "Class '" + node.class_name + "' has no new() constructor but arguments were provided");
    }

    return IRValue{"i8*", obj_ptr, true};
}

CodeGenerator::IRValue CodeGenerator::generate_index_expression(IndexExpression& node) {
    IRValue raw_index = generate_expression(node.index);
    const CodegenClassFieldInfo* object_member_field = nullptr;
    if (node.object && is_node<MemberExpression>(node.object)) {
        const auto& member = get_node<MemberExpression>(node.object);
        const VariableInfo* owner_var = nullptr;
        if (member.object && is_node<Identifier>(member.object)) {
            owner_var = resolve_variable(get_node<Identifier>(member.object).name);
        } else if (member.object && is_node<ThisExpression>(member.object)) {
            owner_var = resolve_variable("this");
        }
        if (owner_var && !owner_var->class_name.empty()) {
            const auto class_it = classes.find(owner_var->class_name);
            if (class_it != classes.end()) {
                const auto field_it = class_it->second.fields.find(member.property);
                if (field_it != class_it->second.fields.end()) {
                    object_member_field = &field_it->second;
                }
            }
        }
    }

    if (node.object && is_node<Identifier>(node.object)) {
        const std::string object_name = get_node<Identifier>(node.object).name;
        const VariableInfo* var = resolve_variable(object_name);
        if (!var) {
            throw std::runtime_error("Unknown variable '" + object_name + "' in index expression");
        }

        if (var->is_fixed_array) {
            IRValue index = cast_value(raw_index, "i64");
            const std::string array_type = "[" + std::to_string(var->fixed_array_size) + " x " +
                                           var->fixed_array_elem_llvm_type + "]";
            const std::string elem_ptr = next_register(object_name + "_elem_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + array_type + ", " + array_type + "* " +
                      var->ptr_value + ", i64 0, i64 " + index.value);

            const std::string loaded = next_register(object_name + "_elem");
            emit_line(loaded + " = load " + var->fixed_array_elem_llvm_type + ", " +
                      var->fixed_array_elem_llvm_type + "* " + elem_ptr);
            return IRValue{var->fixed_array_elem_llvm_type, loaded, true};
        }

        if (var->is_dynamic_array) {
            IRValue index = cast_value(raw_index, "i64");
            const std::string header = next_register(object_name + "_hdr");
            emit_line(header + " = load i8*, i8** " + var->ptr_value);

            const std::string data_slot_raw = next_register(object_name + "_data_slot_raw");
            emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 16");
            const std::string data_slot = next_register(object_name + "_data_slot");
            emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");
            const std::string data_raw = next_register(object_name + "_data_raw");
            emit_line(data_raw + " = load i8*, i8** " + data_slot);

            const std::string typed_data = next_register(object_name + "_typed_data");
            emit_line(typed_data + " = bitcast i8* " + data_raw + " to " +
                      var->dynamic_array_elem_llvm_type + "*");

            const std::string elem_ptr = next_register(object_name + "_elem_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + var->dynamic_array_elem_llvm_type +
                      ", " + var->dynamic_array_elem_llvm_type + "* " + typed_data + ", i64 " +
                      index.value);

            const std::string loaded = next_register(object_name + "_elem");
            emit_line(loaded + " = load " + var->dynamic_array_elem_llvm_type + ", " +
                      var->dynamic_array_elem_llvm_type + "* " + elem_ptr);
            return IRValue{var->dynamic_array_elem_llvm_type, loaded, true};
        }

        if (var->is_dict) {
            IRValue key = cast_value(raw_index, var->dict_key_llvm_type.empty() ? "i8*" : var->dict_key_llvm_type);

            const std::string key_type = var->dict_key_llvm_type.empty() ? "i8*" : var->dict_key_llvm_type;
            const std::string value_type = var->dict_value_llvm_type.empty() ? "i8*" : var->dict_value_llvm_type;

            const std::string header = next_register(object_name + "_dict_hdr");
            emit_line(header + " = load i8*, i8** " + var->ptr_value);

            const std::string len_slot = next_register(object_name + "_dict_len_slot");
            emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
            const std::string len = next_register(object_name + "_dict_len");
            emit_line(len + " = load i64, i64* " + len_slot);

            const std::string keys_slot_raw = next_register(object_name + "_dict_keys_slot_raw");
            emit_line(keys_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 16");
            const std::string keys_slot = next_register(object_name + "_dict_keys_slot");
            emit_line(keys_slot + " = bitcast i8* " + keys_slot_raw + " to i8**");
            const std::string keys_raw = next_register(object_name + "_dict_keys_raw");
            emit_line(keys_raw + " = load i8*, i8** " + keys_slot);
            const std::string keys_typed = next_register(object_name + "_dict_keys_typed");
            emit_line(keys_typed + " = bitcast i8* " + keys_raw + " to " + key_type + "*");

            const std::string values_slot_raw = next_register(object_name + "_dict_values_slot_raw");
            emit_line(values_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 24");
            const std::string values_slot = next_register(object_name + "_dict_values_slot");
            emit_line(values_slot + " = bitcast i8* " + values_slot_raw + " to i8**");
            const std::string values_raw = next_register(object_name + "_dict_values_raw");
            emit_line(values_raw + " = load i8*, i8** " + values_slot);
            const std::string values_typed = next_register(object_name + "_dict_values_typed");
            emit_line(values_typed + " = bitcast i8* " + values_raw + " to " + value_type + "*");

            const std::string idx_ptr = next_register(object_name + "_dict_idx_ptr");
            emit_line(idx_ptr + " = alloca i64");
            emit_line("store i64 0, i64* " + idx_ptr);
            const std::string found_ptr = next_register(object_name + "_dict_found_ptr");
            emit_line(found_ptr + " = alloca i1");
            emit_line("store i1 0, i1* " + found_ptr);
            const std::string result_ptr = next_register(object_name + "_dict_result_ptr");
            emit_line(result_ptr + " = alloca " + value_type);
            std::string default_value = "0";
            if (value_type == "double") {
                default_value = "0.0";
            } else if (is_pointer_type(value_type)) {
                default_value = "null";
            }
            emit_line("store " + value_type + " " + default_value + ", " + value_type + "* " + result_ptr);

            const std::string cond_label = next_label("dict_get_cond");
            const std::string body_label = next_label("dict_get_body");
            const std::string hit_label = next_label("dict_get_hit");
            const std::string next_label_name = next_label("dict_get_next");
            const std::string end_label = next_label("dict_get_end");

            emit_line("br label %" + cond_label);
            emit_label(cond_label);
            const std::string idx = next_register(object_name + "_dict_idx");
            emit_line(idx + " = load i64, i64* " + idx_ptr);
            const std::string cond = next_register(object_name + "_dict_cond");
            emit_line(cond + " = icmp slt i64 " + idx + ", " + len);
            emit_line("br i1 " + cond + ", label %" + body_label + ", label %" + end_label);

            emit_label(body_label);
            const std::string key_ptr = next_register(object_name + "_dict_key_ptr");
            emit_line(key_ptr + " = getelementptr inbounds " + key_type + ", " + key_type + "* " +
                      keys_typed + ", i64 " + idx);
            const std::string key_loaded = next_register(object_name + "_dict_key_loaded");
            emit_line(key_loaded + " = load " + key_type + ", " + key_type + "* " + key_ptr);

            std::string key_match;
            if (key_type == "i8*") {
                const std::string cmp = next_register("dict_key_cmp");
                emit_line(cmp + " = call i32 @strcmp(i8* " + key_loaded + ", i8* " + key.value + ")");
                key_match = next_register("dict_key_match");
                emit_line(key_match + " = icmp eq i32 " + cmp + ", 0");
            } else if (key_type == "double") {
                key_match = next_register("dict_key_match");
                emit_line(key_match + " = fcmp oeq double " + key_loaded + ", " + key.value);
            } else {
                key_match = next_register("dict_key_match");
                emit_line(key_match + " = icmp eq " + key_type + " " + key_loaded + ", " + key.value);
            }
            emit_line("br i1 " + key_match + ", label %" + hit_label + ", label %" + next_label_name);

            emit_label(hit_label);
            const std::string value_ptr = next_register(object_name + "_dict_value_ptr");
            emit_line(value_ptr + " = getelementptr inbounds " + value_type + ", " + value_type + "* " +
                      values_typed + ", i64 " + idx);
            const std::string value_loaded = next_register(object_name + "_dict_value_loaded");
            emit_line(value_loaded + " = load " + value_type + ", " + value_type + "* " + value_ptr);
            emit_line("store " + value_type + " " + value_loaded + ", " + value_type + "* " + result_ptr);
            emit_line("store i1 1, i1* " + found_ptr);
            emit_line("br label %" + end_label);

            emit_label(next_label_name);
            const std::string next_idx = next_register(object_name + "_dict_next_idx");
            emit_line(next_idx + " = add i64 " + idx + ", 1");
            emit_line("store i64 " + next_idx + ", i64* " + idx_ptr);
            emit_line("br label %" + cond_label);

            emit_label(end_label);
            const std::string result = next_register(object_name + "_dict_result");
            emit_line(result + " = load " + value_type + ", " + value_type + "* " + result_ptr);
            return IRValue{value_type, result, true};
        }

        if (var->llvm_type == "i8*" && var->class_name.empty()) {
            IRValue index = cast_value(raw_index, "i64");
            const std::string str_ptr = next_register(object_name + "_str");
            emit_line(str_ptr + " = load i8*, i8** " + var->ptr_value);

            const std::string char_ptr = next_register(object_name + "_char_ptr");
            emit_line(char_ptr + " = getelementptr inbounds i8, i8* " + str_ptr + ", i64 " + index.value);
            const std::string ch = next_register(object_name + "_char");
            emit_line(ch + " = load i8, i8* " + char_ptr);

            const std::string one_char = next_register(object_name + "_one_char");
            emit_line(one_char + " = call i8* @__mt_char(i8 " + ch + ")");
            return IRValue{"i8*", one_char, true};
        }
    }

    IRValue object = cast_value(generate_expression(node.object), "i8*");
    if (raw_index.type == "i8*") {
        // Dict lookup placeholder for bootstrap parity.
        return IRValue{"i8*", "null", true};
    }

    IRValue index = cast_value(raw_index, "i64");
    if (object_member_field && object_member_field->mt_type == "array") {
        const std::string data_slot_raw = next_register("idx_data_slot_raw");
        emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + object.value + ", i64 16");
        const std::string data_slot = next_register("idx_data_slot");
        emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");
        const std::string data_raw = next_register("idx_data_raw");
        emit_line(data_raw + " = load i8*, i8** " + data_slot);
        const std::string typed_data = next_register("idx_typed_data");
        emit_line(typed_data + " = bitcast i8* " + data_raw + " to i8**");
        const std::string elem_ptr = next_register("idx_elem_ptr");
        emit_line(elem_ptr + " = getelementptr inbounds i8*, i8** " + typed_data + ", i64 " + index.value);
        const std::string loaded = next_register("idx_elem");
        emit_line(loaded + " = load i8*, i8** " + elem_ptr);
        return IRValue{"i8*", loaded, true};
    }

    const std::string char_ptr = next_register("idx_char_ptr");
    emit_line(char_ptr + " = getelementptr inbounds i8, i8* " + object.value + ", i64 " + index.value);
    const std::string ch = next_register("idx_char");
    emit_line(ch + " = load i8, i8* " + char_ptr);
    const std::string one_char = next_register("idx_one_char");
    emit_line(one_char + " = call i8* @__mt_char(i8 " + ch + ")");
    return IRValue{"i8*", one_char, true};
}

CodeGenerator::IRValue CodeGenerator::generate_string_concat(IRValue lhs, IRValue rhs) {
    lhs = cast_value(lhs, "i8*");
    rhs = cast_value(rhs, "i8*");

    IRValue lhs_len = emit_call("i64", "@strlen", {lhs});
    IRValue rhs_len = emit_call("i64", "@strlen", {rhs});

    const std::string total_len = next_register("strcat_total_len");
    emit_line(total_len + " = add i64 " + lhs_len.value + ", " + rhs_len.value);
    const std::string with_null = next_register("strcat_with_null");
    emit_line(with_null + " = add i64 " + total_len + ", 1");

    IRValue size{"i64", with_null, true};
    IRValue buffer = emit_call("i8*", "@malloc", {size});
    (void)emit_call("i8*", "@strcpy", {buffer, lhs});
    (void)emit_call("i8*", "@strcat", {buffer, rhs});
    return buffer;
}

CodeGenerator::IRValue CodeGenerator::generate_binary_expression(BinaryExpression& node) {
    const std::string op = node.op.value;

    if (op == "&&" || op == "||") {
        IRValue lhs_bool = ensure_boolean(generate_expression(node.left));
        IRValue rhs_bool = ensure_boolean(generate_expression(node.right));

        const std::string reg = next_register(op == "&&" ? "and" : "or");
        emit_line(reg + " = " + std::string(op == "&&" ? "and" : "or") + " i1 " +
                  lhs_bool.value + ", " + rhs_bool.value);
        return IRValue{"i1", reg, true};
    }

    IRValue lhs = generate_expression(node.left);
    IRValue rhs = generate_expression(node.right);

    auto is_null_ptr = [](const IRValue& value) -> bool {
        return value.type == "i8*" && value.value == "null";
    };
    auto zero_for_type = [](const std::string& type) -> IRValue {
        if (type == "double") {
            return IRValue{"double", "0.0", true};
        }
        if (type == "i1") {
            return IRValue{"i1", "0", true};
        }
        if (type == "i64") {
            return IRValue{"i64", "0", true};
        }
        return IRValue{"i32", "0", true};
    };

    if (is_null_ptr(lhs) && (rhs.type == "i32" || rhs.type == "double" || rhs.type == "i1")) {
        lhs = zero_for_type(rhs.type);
    } else if (is_null_ptr(rhs) && (lhs.type == "i32" || lhs.type == "double" || lhs.type == "i1")) {
        rhs = zero_for_type(lhs.type);
    }

    if (op == "+" && lhs.type == "i8*" && rhs.type == "i8*") {
        return generate_string_concat(std::move(lhs), std::move(rhs));
    }

    if ((op == "==" || op == "!=") && lhs.type == "i8*" && rhs.type == "i8*") {
        const std::string lhs_is_null = next_register("lhs_is_null");
        emit_line(lhs_is_null + " = icmp eq i8* " + lhs.value + ", null");
        const std::string rhs_is_null = next_register("rhs_is_null");
        emit_line(rhs_is_null + " = icmp eq i8* " + rhs.value + ", null");
        const std::string both_null = next_register("both_null");
        emit_line(both_null + " = and i1 " + lhs_is_null + ", " + rhs_is_null);
        const std::string either_null = next_register("either_null");
        emit_line(either_null + " = or i1 " + lhs_is_null + ", " + rhs_is_null);

        const std::string null_label = next_label("strcmp_null");
        const std::string cmp_label = next_label("strcmp_cmp");
        const std::string end_label = next_label("strcmp_end");
        const std::string result_ptr = next_register("strcmp_result_addr");
        emit_line(result_ptr + " = alloca i1");
        emit_line("br i1 " + either_null + ", label %" + null_label + ", label %" + cmp_label);

        emit_label(null_label);
        if (op == "==") {
            emit_line("store i1 " + both_null + ", i1* " + result_ptr);
        } else {
            const std::string not_both_null = next_register("not_both_null");
            emit_line(not_both_null + " = xor i1 " + both_null + ", 1");
            emit_line("store i1 " + not_both_null + ", i1* " + result_ptr);
        }
        emit_line("br label %" + end_label);

        emit_label(cmp_label);
        IRValue cmp = emit_call("i32", "@strcmp", {lhs, rhs});
        const std::string reg = next_register("str_cmp");
        emit_line(reg + " = icmp " + std::string(op == "==" ? "eq" : "ne") +
                  " i32 " + cmp.value + ", 0");
        emit_line("store i1 " + reg + ", i1* " + result_ptr);
        emit_line("br label %" + end_label);

        emit_label(end_label);
        const std::string result = next_register("str_cmp_result");
        emit_line(result + " = load i1, i1* " + result_ptr);
        return IRValue{"i1", result, true};
    }

    std::string common_type = lhs.type;
    if (lhs.type == "double" || rhs.type == "double") {
        common_type = "double";
    } else if (lhs.type == "i32" || rhs.type == "i32") {
        common_type = "i32";
    } else if (lhs.type == "i1" && rhs.type == "i1") {
        common_type = "i1";
    }

    lhs = cast_value(lhs, common_type);
    rhs = cast_value(rhs, common_type);

    if (op == "+" || op == "-" || op == "*" || op == "/") {
        std::string ir_op;
        if (common_type == "double") {
            ir_op = (op == "+") ? "fadd" : (op == "-") ? "fsub" : (op == "*") ? "fmul" : "fdiv";
        } else {
            ir_op = (op == "+") ? "add" : (op == "-") ? "sub" : (op == "*") ? "mul" : "sdiv";
        }

        const std::string reg = next_register("binop");
        emit_line(reg + " = " + ir_op + " " + common_type + " " + lhs.value + ", " + rhs.value);
        return IRValue{common_type, reg, true};
    }

    if (op == "==" || op == "!=" || op == ">" || op == "<" || op == ">=" || op == "<=") {
        std::string cmp_op;
        if (op == "==") cmp_op = "eq";
        if (op == "!=") cmp_op = "ne";
        if (op == ">") cmp_op = "sgt";
        if (op == "<") cmp_op = "slt";
        if (op == ">=") cmp_op = "sge";
        if (op == "<=") cmp_op = "sle";

        const std::string reg = next_register("cmp");
        if (common_type == "double") {
            const std::string fcmp =
                (op == "==") ? "oeq" : (op == "!=") ? "one" : (op == ">") ? "ogt"
                : (op == "<") ? "olt" : (op == ">=") ? "oge" : "ole";
            emit_line(reg + " = fcmp " + fcmp + " double " + lhs.value + ", " + rhs.value);
        } else {
            emit_line(reg + " = icmp " + cmp_op + " " + common_type + " " + lhs.value + ", " + rhs.value);
        }
        return IRValue{"i1", reg, true};
    }

    throw std::runtime_error("Unsupported binary operator '" + op + "'");
}

CodeGenerator::IRValue CodeGenerator::generate_in_expression(InExpression& node) {
    auto compare_with_item = [this](const std::string& elem_type,
                                    const std::string& elem_value,
                                    const IRValue& item) -> std::string {
        if (elem_type == "i8*") {
            IRValue item_str = cast_value(item, "i8*");
            IRValue cmp = emit_call("i32", "@strcmp", {IRValue{"i8*", elem_value, true}, item_str});
            const std::string is_eq = next_register("in_eq");
            emit_line(is_eq + " = icmp eq i32 " + cmp.value + ", 0");
            return is_eq;
        }
        if (elem_type == "double") {
            IRValue item_num = cast_value(item, "double");
            const std::string is_eq = next_register("in_eq");
            emit_line(is_eq + " = fcmp oeq double " + elem_value + ", " + item_num.value);
            return is_eq;
        }
        if (elem_type == "i1") {
            IRValue item_bool = cast_value(item, "i1");
            const std::string is_eq = next_register("in_eq");
            emit_line(is_eq + " = icmp eq i1 " + elem_value + ", " + item_bool.value);
            return is_eq;
        }
        IRValue item_int = cast_value(item, elem_type);
        const std::string is_eq = next_register("in_eq");
        emit_line(is_eq + " = icmp eq " + elem_type + " " + elem_value + ", " + item_int.value);
        return is_eq;
    };

    IRValue item = generate_expression(node.item);

    const VariableInfo* var = nullptr;
    std::string container_name = "container";
    if (node.container && is_node<Identifier>(node.container)) {
        container_name = get_node<Identifier>(node.container).name;
        var = resolve_variable(container_name);
    }

    if (var && (var->is_fixed_array || var->is_dynamic_array)) {
        const bool is_fixed = var->is_fixed_array;
        const std::string elem_type = is_fixed ? var->fixed_array_elem_llvm_type : var->dynamic_array_elem_llvm_type;

        const std::string result_ptr = next_register("in_result_addr");
        emit_line(result_ptr + " = alloca i1");
        emit_line("store i1 0, i1* " + result_ptr);

        const std::string idx_ptr = next_register("in_idx_addr");
        emit_line(idx_ptr + " = alloca i64");
        emit_line("store i64 0, i64* " + idx_ptr);

        const std::string cond_label = next_label("in_cond");
        const std::string body_label = next_label("in_body");
        const std::string step_label = next_label("in_step");
        const std::string found_label = next_label("in_found");
        const std::string end_label = next_label("in_end");

        emit_line("br label %" + cond_label);
        emit_label(cond_label);
        const std::string idx = next_register("in_idx");
        emit_line(idx + " = load i64, i64* " + idx_ptr);

        std::string len_value;
        if (is_fixed) {
            len_value = std::to_string(var->fixed_array_size);
        } else {
            const std::string header = next_register(container_name + "_hdr");
            emit_line(header + " = load i8*, i8** " + var->ptr_value);
            const std::string len_slot = next_register(container_name + "_len_slot");
            emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
            const std::string dyn_len = next_register(container_name + "_len");
            emit_line(dyn_len + " = load i64, i64* " + len_slot);
            len_value = dyn_len;
        }

        const std::string cond = next_register("in_cmp");
        emit_line(cond + " = icmp slt i64 " + idx + ", " + len_value);
        emit_line("br i1 " + cond + ", label %" + body_label + ", label %" + end_label);

        emit_label(body_label);
        std::string elem_value;
        if (is_fixed) {
            const std::string array_type = "[" + std::to_string(var->fixed_array_size) + " x " +
                                           var->fixed_array_elem_llvm_type + "]";
            const std::string elem_ptr = next_register(container_name + "_in_elem_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + array_type + ", " + array_type + "* " +
                      var->ptr_value + ", i64 0, i64 " + idx);
            const std::string elem_val = next_register(container_name + "_in_elem");
            emit_line(elem_val + " = load " + elem_type + ", " + elem_type + "* " + elem_ptr);
            elem_value = elem_val;
        } else {
            const std::string header = next_register(container_name + "_in_hdr");
            emit_line(header + " = load i8*, i8** " + var->ptr_value);
            const std::string data_slot_raw = next_register(container_name + "_in_data_slot_raw");
            emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 16");
            const std::string data_slot = next_register(container_name + "_in_data_slot");
            emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");
            const std::string data_raw = next_register(container_name + "_in_data_raw");
            emit_line(data_raw + " = load i8*, i8** " + data_slot);
            const std::string typed_data = next_register(container_name + "_in_typed_data");
            emit_line(typed_data + " = bitcast i8* " + data_raw + " to " + elem_type + "*");
            const std::string elem_ptr = next_register(container_name + "_in_elem_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + elem_type + ", " + elem_type + "* " +
                      typed_data + ", i64 " + idx);
            const std::string elem_val = next_register(container_name + "_in_elem");
            emit_line(elem_val + " = load " + elem_type + ", " + elem_type + "* " + elem_ptr);
            elem_value = elem_val;
        }

        const std::string is_eq = compare_with_item(elem_type, elem_value, item);
        emit_line("br i1 " + is_eq + ", label %" + found_label + ", label %" + step_label);

        emit_label(step_label);
        const std::string next_idx = next_register("in_next");
        emit_line(next_idx + " = add i64 " + idx + ", 1");
        emit_line("store i64 " + next_idx + ", i64* " + idx_ptr);
        emit_line("br label %" + cond_label);

        emit_label(found_label);
        emit_line("store i1 1, i1* " + result_ptr);
        emit_line("br label %" + end_label);

        emit_label(end_label);
        const std::string result = next_register("in_result");
        emit_line(result + " = load i1, i1* " + result_ptr);
        return IRValue{"i1", result, true};
    }

    if (var && var->is_dict) {
        const std::string key_type = var->dict_key_llvm_type.empty() ? "i8*" : var->dict_key_llvm_type;
        IRValue key = cast_value(item, key_type);

        const std::string header = next_register(container_name + "_dict_hdr");
        emit_line(header + " = load i8*, i8** " + var->ptr_value);
        const std::string len_slot = next_register(container_name + "_dict_len_slot");
        emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
        const std::string len = next_register(container_name + "_dict_len");
        emit_line(len + " = load i64, i64* " + len_slot);

        const std::string keys_slot_raw = next_register(container_name + "_dict_keys_slot_raw");
        emit_line(keys_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 16");
        const std::string keys_slot = next_register(container_name + "_dict_keys_slot");
        emit_line(keys_slot + " = bitcast i8* " + keys_slot_raw + " to i8**");
        const std::string keys_raw = next_register(container_name + "_dict_keys_raw");
        emit_line(keys_raw + " = load i8*, i8** " + keys_slot);
        const std::string keys_typed = next_register(container_name + "_dict_keys_typed");
        emit_line(keys_typed + " = bitcast i8* " + keys_raw + " to " + key_type + "*");

        const std::string idx_ptr = next_register("in_dict_idx_addr");
        emit_line(idx_ptr + " = alloca i64");
        emit_line("store i64 0, i64* " + idx_ptr);

        const std::string result_ptr = next_register("in_dict_result_addr");
        emit_line(result_ptr + " = alloca i1");
        emit_line("store i1 0, i1* " + result_ptr);

        const std::string cond_label = next_label("in_dict_cond");
        const std::string body_label = next_label("in_dict_body");
        const std::string next_label_name = next_label("in_dict_next");
        const std::string found_label = next_label("in_dict_found");
        const std::string end_label = next_label("in_dict_end");

        emit_line("br label %" + cond_label);
        emit_label(cond_label);
        const std::string idx = next_register("in_dict_idx");
        emit_line(idx + " = load i64, i64* " + idx_ptr);
        const std::string cond = next_register("in_dict_cmp");
        emit_line(cond + " = icmp slt i64 " + idx + ", " + len);
        emit_line("br i1 " + cond + ", label %" + body_label + ", label %" + end_label);

        emit_label(body_label);
        const std::string elem_ptr = next_register("in_dict_key_ptr");
        emit_line(elem_ptr + " = getelementptr inbounds " + key_type + ", " + key_type + "* " +
                  keys_typed + ", i64 " + idx);
        const std::string elem = next_register("in_dict_key");
        emit_line(elem + " = load " + key_type + ", " + key_type + "* " + elem_ptr);

        std::string is_eq;
        if (key_type == "i8*") {
            const std::string cmp = next_register("in_dict_key_cmp");
            emit_line(cmp + " = call i32 @strcmp(i8* " + elem + ", i8* " + key.value + ")");
            is_eq = next_register("in_dict_key_eq");
            emit_line(is_eq + " = icmp eq i32 " + cmp + ", 0");
        } else if (key_type == "double") {
            is_eq = next_register("in_dict_key_eq");
            emit_line(is_eq + " = fcmp oeq double " + elem + ", " + key.value);
        } else {
            is_eq = next_register("in_dict_key_eq");
            emit_line(is_eq + " = icmp eq " + key_type + " " + elem + ", " + key.value);
        }
        emit_line("br i1 " + is_eq + ", label %" + found_label + ", label %" + next_label_name);

        emit_label(next_label_name);
        const std::string next_idx = next_register("in_dict_next_idx");
        emit_line(next_idx + " = add i64 " + idx + ", 1");
        emit_line("store i64 " + next_idx + ", i64* " + idx_ptr);
        emit_line("br label %" + cond_label);

        emit_label(found_label);
        emit_line("store i1 1, i1* " + result_ptr);
        emit_line("br label %" + end_label);

        emit_label(end_label);
        const std::string result = next_register("in_dict_result");
        emit_line(result + " = load i1, i1* " + result_ptr);
        return IRValue{"i1", result, true};
    }

    IRValue container = generate_expression(node.container);
    if (container.type == "i8*" && item.type == "i8*") {
        IRValue found = emit_call("i8*", "@strstr", {container, item});
        return ensure_boolean(found);
    }

    return IRValue{"i1", "0", true};
}

CodeGenerator::IRValue CodeGenerator::emit_call(const std::string& return_type,
                                                const std::string& callee,
                                                const std::vector<IRValue>& args,
                                                bool) {
    std::vector<std::string> formatted_args;
    formatted_args.reserve(args.size());
    for (const auto& arg : args) {
        formatted_args.push_back(arg.type + " " + arg.value);
    }

    if (return_type == "void") {
        emit_line("call void " + callee + "(" + join_params(formatted_args) + ")");
        return IRValue{"void", "", true};
    }

    const std::string reg = next_register("call");
    emit_line(reg + " = call " + return_type + " " + callee + "(" + join_params(formatted_args) + ")");
    return IRValue{return_type, reg, true};
}

CodeGenerator::IRValue CodeGenerator::generate_call_expression(CallExpression& node) {
    auto emit_strlen_for_string = [this](const IRValue& value) -> IRValue {
        if (value.type != "i8*") {
            throw std::runtime_error("length() for strings expects i8* value");
        }
        IRValue len_i64 = emit_call("i64", "@strlen", {value});
        const std::string len_i32 = next_register("strlen_i32");
        emit_line(len_i32 + " = trunc i64 " + len_i64.value + " to i32");
        return IRValue{"i32", len_i32, true};
    };

    auto emit_dynamic_array_length =
        [this](const std::string& object_name, const VariableInfo* var) -> IRValue {
            if (!var || !var->is_dynamic_array) {
                throw std::runtime_error("Internal error: expected dynamic array variable");
            }
            const std::string header = next_register(object_name + "_hdr");
            emit_line(header + " = load i8*, i8** " + var->ptr_value);
            const std::string len_slot = next_register(object_name + "_len_slot");
            emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
            const std::string len_i64 = next_register(object_name + "_len_i64");
            emit_line(len_i64 + " = load i64, i64* " + len_slot);
            const std::string len_i32 = next_register(object_name + "_len_i32");
            emit_line(len_i32 + " = trunc i64 " + len_i64 + " to i32");
            return IRValue{"i32", len_i32, true};
        };

    auto emit_dynamic_array_append =
        [this](const std::string& object_name, const VariableInfo* var, ASTNode& value_node) -> IRValue {
            if (!var || !var->is_dynamic_array) {
                throw std::runtime_error("Internal error: expected dynamic array variable");
            }

            std::size_t elem_size = 8;
            if (var->dynamic_array_elem_llvm_type == "i1") {
                elem_size = 1;
            } else if (var->dynamic_array_elem_llvm_type == "i32") {
                elem_size = 4;
            } else if (var->dynamic_array_elem_llvm_type == "double") {
                elem_size = 8;
            }

            const std::string header = next_register(object_name + "_hdr");
            emit_line(header + " = load i8*, i8** " + var->ptr_value);

            const std::string len_slot = next_register(object_name + "_len_slot");
            emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
            const std::string cap_slot = next_register(object_name + "_cap_slot");
            emit_line(cap_slot + " = getelementptr inbounds i64, i64* " + len_slot + ", i64 1");
            const std::string data_slot_raw = next_register(object_name + "_data_slot_raw");
            emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 16");
            const std::string data_slot = next_register(object_name + "_data_slot");
            emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");

            const std::string len = next_register(object_name + "_len");
            emit_line(len + " = load i64, i64* " + len_slot);
            const std::string cap = next_register(object_name + "_cap");
            emit_line(cap + " = load i64, i64* " + cap_slot);

            const std::string need_grow = next_register(object_name + "_need_grow");
            emit_line(need_grow + " = icmp eq i64 " + len + ", " + cap);

            const std::string grow_label = next_label("arr_append_grow");
            const std::string cont_label = next_label("arr_append_cont");
            emit_line("br i1 " + need_grow + ", label %" + grow_label + ", label %" + cont_label);

            emit_label(grow_label);
            const std::string doubled_cap = next_register(object_name + "_doubled_cap");
            emit_line(doubled_cap + " = mul i64 " + cap + ", 2");
            const std::string cap_is_zero = next_register(object_name + "_cap_is_zero");
            emit_line(cap_is_zero + " = icmp eq i64 " + doubled_cap + ", 0");
            const std::string new_cap = next_register(object_name + "_new_cap");
            emit_line(new_cap + " = select i1 " + cap_is_zero + ", i64 4, i64 " + doubled_cap);

            const std::string old_data = next_register(object_name + "_old_data");
            emit_line(old_data + " = load i8*, i8** " + data_slot);
            const std::string new_bytes = next_register(object_name + "_new_bytes");
            emit_line(new_bytes + " = mul i64 " + new_cap + ", " + std::to_string(elem_size));
            const std::string new_data = next_register(object_name + "_new_data");
            emit_line(new_data + " = call i8* @realloc(i8* " + old_data + ", i64 " + new_bytes + ")");
            emit_line("store i8* " + new_data + ", i8** " + data_slot);
            emit_line("store i64 " + new_cap + ", i64* " + cap_slot);
            emit_line("br label %" + cont_label);

            emit_label(cont_label);
            const std::string len2 = next_register(object_name + "_len2");
            emit_line(len2 + " = load i64, i64* " + len_slot);
            const std::string data_raw = next_register(object_name + "_data_raw");
            emit_line(data_raw + " = load i8*, i8** " + data_slot);
            const std::string typed_data = next_register(object_name + "_typed_data");
            emit_line(typed_data + " = bitcast i8* " + data_raw + " to " +
                      var->dynamic_array_elem_llvm_type + "*");
            const std::string elem_ptr = next_register(object_name + "_append_elem_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + var->dynamic_array_elem_llvm_type + ", " +
                      var->dynamic_array_elem_llvm_type + "* " + typed_data + ", i64 " + len2);

            IRValue value = cast_value(generate_expression(value_node), var->dynamic_array_elem_llvm_type);
            emit_line("store " + var->dynamic_array_elem_llvm_type + " " + value.value + ", " +
                      var->dynamic_array_elem_llvm_type + "* " + elem_ptr);

            const std::string new_len = next_register(object_name + "_new_len");
            emit_line(new_len + " = add i64 " + len2 + ", 1");
            emit_line("store i64 " + new_len + ", i64* " + len_slot);

            return IRValue{"void", "", true};
        };

    auto emit_dynamic_array_pop =
        [this](const std::string& object_name, const VariableInfo* var,
               ASTNode* index_node = nullptr) -> IRValue {
            if (!var || !var->is_dynamic_array) {
                throw std::runtime_error("Internal error: expected dynamic array variable");
            }

            std::size_t elem_size = 8;
            if (var->dynamic_array_elem_llvm_type == "i1") {
                elem_size = 1;
            } else if (var->dynamic_array_elem_llvm_type == "i32") {
                elem_size = 4;
            } else if (var->dynamic_array_elem_llvm_type == "double") {
                elem_size = 8;
            }

            const std::string header = next_register(object_name + "_hdr");
            emit_line(header + " = load i8*, i8** " + var->ptr_value);
            const std::string len_slot = next_register(object_name + "_len_slot");
            emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
            const std::string cap_slot = next_register(object_name + "_cap_slot");
            emit_line(cap_slot + " = getelementptr inbounds i64, i64* " + len_slot + ", i64 1");
            const std::string len = next_register(object_name + "_len");
            emit_line(len + " = load i64, i64* " + len_slot);
            const std::string cap = next_register(object_name + "_cap");
            emit_line(cap + " = load i64, i64* " + cap_slot);

            const std::string pop_tmp = next_register(object_name + "_pop_tmp");
            emit_line(pop_tmp + " = alloca " + var->dynamic_array_elem_llvm_type);

            std::string default_value = "0";
            if (var->dynamic_array_elem_llvm_type == "double") {
                default_value = "0.0";
            } else if (is_pointer_type(var->dynamic_array_elem_llvm_type)) {
                default_value = "null";
            }
            emit_line("store " + var->dynamic_array_elem_llvm_type + " " + default_value + ", " +
                      var->dynamic_array_elem_llvm_type + "* " + pop_tmp);

            const std::string is_empty = next_register(object_name + "_is_empty");
            emit_line(is_empty + " = icmp eq i64 " + len + ", 0");
            const std::string empty_label = next_label("arr_pop_empty");
            const std::string value_label = next_label("arr_pop_value");
            const std::string end_label = next_label("arr_pop_end");
            emit_line("br i1 " + is_empty + ", label %" + empty_label + ", label %" + value_label);

            emit_label(empty_label);
            emit_line("br label %" + end_label);

            emit_label(value_label);
            std::string pop_index;
            if (index_node != nullptr) {
                IRValue raw_index = cast_value(generate_expression(*index_node), "i64");
                const std::string index_negative = next_register(object_name + "_index_negative");
                emit_line(index_negative + " = icmp slt i64 " + raw_index.value + ", 0");
                const std::string index_too_large = next_register(object_name + "_index_too_large");
                emit_line(index_too_large + " = icmp sge i64 " + raw_index.value + ", " + len);
                const std::string index_invalid = next_register(object_name + "_index_invalid");
                emit_line(index_invalid + " = or i1 " + index_negative + ", " + index_too_large);

                const std::string invalid_index_label = next_label("arr_pop_invalid_index");
                const std::string valid_index_label = next_label("arr_pop_valid_index");
                emit_line("br i1 " + index_invalid + ", label %" + invalid_index_label +
                          ", label %" + valid_index_label);

                emit_label(invalid_index_label);
                emit_line("br label %" + end_label);

                emit_label(valid_index_label);
                pop_index = raw_index.value;
            } else {
                const std::string last_index = next_register(object_name + "_last_index");
                emit_line(last_index + " = sub i64 " + len + ", 1");
                pop_index = last_index;
            }

            const std::string new_len = next_register(object_name + "_new_len");
            emit_line(new_len + " = sub i64 " + len + ", 1");

            const std::string data_slot_raw = next_register(object_name + "_data_slot_raw");
            emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 16");
            const std::string data_slot = next_register(object_name + "_data_slot");
            emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");
            const std::string data_raw = next_register(object_name + "_data_raw");
            emit_line(data_raw + " = load i8*, i8** " + data_slot);
            const std::string typed_data = next_register(object_name + "_typed_data");
            emit_line(typed_data + " = bitcast i8* " + data_raw + " to " +
                      var->dynamic_array_elem_llvm_type + "*");

            const std::string elem_ptr = next_register(object_name + "_pop_elem_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + var->dynamic_array_elem_llvm_type + ", " +
                      var->dynamic_array_elem_llvm_type + "* " + typed_data + ", i64 " + pop_index);
            const std::string elem_value = next_register(object_name + "_pop_elem_value");
            emit_line(elem_value + " = load " + var->dynamic_array_elem_llvm_type + ", " +
                      var->dynamic_array_elem_llvm_type + "* " + elem_ptr);
            emit_line("store " + var->dynamic_array_elem_llvm_type + " " + elem_value + ", " +
                      var->dynamic_array_elem_llvm_type + "* " + pop_tmp);

            const std::string needs_shift = next_register(object_name + "_needs_shift");
            emit_line(needs_shift + " = icmp slt i64 " + pop_index + ", " + new_len);
            const std::string shift_label = next_label("arr_pop_shift");
            const std::string no_shift_label = next_label("arr_pop_no_shift");
            const std::string after_shift_label = next_label("arr_pop_after_shift");
            emit_line("br i1 " + needs_shift + ", label %" + shift_label + ", label %" + no_shift_label);

            emit_label(shift_label);
            const std::string shift_idx_ptr = next_register(object_name + "_shift_idx_ptr");
            emit_line(shift_idx_ptr + " = alloca i64");
            emit_line("store i64 " + pop_index + ", i64* " + shift_idx_ptr);

            const std::string shift_cond_label = next_label("arr_pop_shift_cond");
            const std::string shift_body_label = next_label("arr_pop_shift_body");
            const std::string shift_end_label = next_label("arr_pop_shift_end");
            emit_line("br label %" + shift_cond_label);

            emit_label(shift_cond_label);
            const std::string shift_i = next_register(object_name + "_shift_i");
            emit_line(shift_i + " = load i64, i64* " + shift_idx_ptr);
            const std::string shift_cond = next_register(object_name + "_shift_cond");
            emit_line(shift_cond + " = icmp slt i64 " + shift_i + ", " + new_len);
            emit_line("br i1 " + shift_cond + ", label %" + shift_body_label + ", label %" +
                      shift_end_label);

            emit_label(shift_body_label);
            const std::string src_index = next_register(object_name + "_shift_src_index");
            emit_line(src_index + " = add i64 " + shift_i + ", 1");
            const std::string src_ptr = next_register(object_name + "_shift_src_ptr");
            emit_line(src_ptr + " = getelementptr inbounds " + var->dynamic_array_elem_llvm_type +
                      ", " + var->dynamic_array_elem_llvm_type + "* " + typed_data + ", i64 " +
                      src_index);
            const std::string src_val = next_register(object_name + "_shift_src_val");
            emit_line(src_val + " = load " + var->dynamic_array_elem_llvm_type + ", " +
                      var->dynamic_array_elem_llvm_type + "* " + src_ptr);
            const std::string dst_ptr = next_register(object_name + "_shift_dst_ptr");
            emit_line(dst_ptr + " = getelementptr inbounds " + var->dynamic_array_elem_llvm_type +
                      ", " + var->dynamic_array_elem_llvm_type + "* " + typed_data + ", i64 " +
                      shift_i);
            emit_line("store " + var->dynamic_array_elem_llvm_type + " " + src_val + ", " +
                      var->dynamic_array_elem_llvm_type + "* " + dst_ptr);
            const std::string next_i = next_register(object_name + "_shift_next_i");
            emit_line(next_i + " = add i64 " + shift_i + ", 1");
            emit_line("store i64 " + next_i + ", i64* " + shift_idx_ptr);
            emit_line("br label %" + shift_cond_label);

            emit_label(shift_end_label);
            emit_line("br label %" + after_shift_label);

            emit_label(no_shift_label);
            emit_line("br label %" + after_shift_label);

            emit_label(after_shift_label);
            emit_line("store i64 " + new_len + ", i64* " + len_slot);

            const std::string can_shrink = next_register(object_name + "_can_shrink");
            emit_line(can_shrink + " = icmp sgt i64 " + cap + ", 8");
            const std::string utilization4 = next_register(object_name + "_util4");
            emit_line(utilization4 + " = mul i64 " + new_len + ", 4");
            const std::string underutilized = next_register(object_name + "_underutilized");
            emit_line(underutilized + " = icmp ule i64 " + utilization4 + ", " + cap);
            const std::string should_shrink = next_register(object_name + "_should_shrink");
            emit_line(should_shrink + " = and i1 " + can_shrink + ", " + underutilized);

            const std::string shrink_label = next_label("arr_pop_shrink");
            emit_line("br i1 " + should_shrink + ", label %" + shrink_label + ", label %" + end_label);

            emit_label(shrink_label);
            const std::string half_cap = next_register(object_name + "_half_cap");
            emit_line(half_cap + " = lshr i64 " + cap + ", 1");
            const std::string half_cap_small = next_register(object_name + "_half_cap_small");
            emit_line(half_cap_small + " = icmp ult i64 " + half_cap + ", 4");
            const std::string new_cap = next_register(object_name + "_new_cap");
            emit_line(new_cap + " = select i1 " + half_cap_small + ", i64 4, i64 " + half_cap);

            const std::string old_data = next_register(object_name + "_old_data");
            emit_line(old_data + " = load i8*, i8** " + data_slot);
            const std::string new_bytes = next_register(object_name + "_new_bytes");
            emit_line(new_bytes + " = mul i64 " + new_cap + ", " + std::to_string(elem_size));
            const std::string shrunk_data = next_register(object_name + "_shrunk_data");
            emit_line(shrunk_data + " = call i8* @realloc(i8* " + old_data + ", i64 " + new_bytes + ")");
            emit_line("store i8* " + shrunk_data + ", i8** " + data_slot);
            emit_line("store i64 " + new_cap + ", i64* " + cap_slot);
            emit_line("br label %" + end_label);

            emit_label(end_label);
            const std::string pop_result = next_register(object_name + "_pop_result");
            emit_line(pop_result + " = load " + var->dynamic_array_elem_llvm_type + ", " +
                      var->dynamic_array_elem_llvm_type + "* " + pop_tmp);
            return IRValue{var->dynamic_array_elem_llvm_type, pop_result, true};
        };

    auto emit_scalar_to_string =
        [this](const std::string& llvm_type, const std::string& value_reg,
               const std::string& prefix) -> std::string {
            if (llvm_type == "i8*") {
                StringConstantInfo null_str = get_or_create_string_constant("null");
                const std::string is_null = next_register(prefix + "_is_null");
                emit_line(is_null + " = icmp eq i8* " + value_reg + ", null");
                const std::string selected = next_register(prefix + "_str");
                emit_line(selected + " = select i1 " + is_null + ", i8* " +
                          string_constant_gep(null_str) + ", i8* " + value_reg);
                return selected;
            }
            if (llvm_type == "i1") {
                StringConstantInfo true_str = get_or_create_string_constant("true");
                StringConstantInfo false_str = get_or_create_string_constant("false");
                const std::string selected = next_register(prefix + "_str");
                emit_line(selected + " = select i1 " + value_reg + ", i8* " +
                          string_constant_gep(true_str) + ", i8* " + string_constant_gep(false_str));
                return selected;
            }

            std::string fmt = "%d";
            std::string scalar_value = value_reg;
            if (llvm_type == "double") {
                fmt = "%g";
            } else if (llvm_type != "i32") {
                if (llvm_type == "i64") {
                    const std::string narrowed = next_register(prefix + "_narrow");
                    emit_line(narrowed + " = trunc i64 " + value_reg + " to i32");
                    scalar_value = narrowed;
                } else {
                    const std::string as_i32 = next_register(prefix + "_as_i32");
                    emit_line(as_i32 + " = zext " + llvm_type + " " + value_reg + " to i32");
                    scalar_value = as_i32;
                }
            }

            const std::string buf_size = (llvm_type == "double") ? "64" : "32";
            const std::string buf = next_register(prefix + "_buf");
            emit_line(buf + " = call i8* @malloc(i64 " + buf_size + ")");
            StringConstantInfo fmt_const = get_or_create_string_constant(fmt);
            IRValue fmt_ptr{"i8*", string_constant_gep(fmt_const), true};
            IRValue buf_arg{"i8*", buf, true};
            IRValue value_arg{llvm_type == "double" ? "double" : "i32", scalar_value, true};
            (void)emit_call("i32", "@sprintf", {buf_arg, fmt_ptr, value_arg}, true);
            return buf;
        };

    auto emit_dict_to_string =
        [this, &emit_scalar_to_string](const std::string& object_name, const VariableInfo* var) -> IRValue {
            if (!var || !var->is_dict) {
                return IRValue{"i8*", "null", true};
            }

            const std::string key_type = var->dict_key_llvm_type.empty() ? "i8*" : var->dict_key_llvm_type;
            const std::string value_type = var->dict_value_llvm_type.empty() ? "i8*" : var->dict_value_llvm_type;

            const std::string header = next_register(object_name + "_dict_hdr");
            emit_line(header + " = load i8*, i8** " + var->ptr_value);
            const std::string len_slot = next_register(object_name + "_dict_len_slot");
            emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
            const std::string len = next_register(object_name + "_dict_len");
            emit_line(len + " = load i64, i64* " + len_slot);

            const std::string keys_slot_raw = next_register(object_name + "_dict_keys_slot_raw");
            emit_line(keys_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 16");
            const std::string keys_slot = next_register(object_name + "_dict_keys_slot");
            emit_line(keys_slot + " = bitcast i8* " + keys_slot_raw + " to i8**");
            const std::string keys_raw = next_register(object_name + "_dict_keys_raw");
            emit_line(keys_raw + " = load i8*, i8** " + keys_slot);
            const std::string keys_typed = next_register(object_name + "_dict_keys_typed");
            emit_line(keys_typed + " = bitcast i8* " + keys_raw + " to " + key_type + "*");

            const std::string values_slot_raw = next_register(object_name + "_dict_values_slot_raw");
            emit_line(values_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 24");
            const std::string values_slot = next_register(object_name + "_dict_values_slot");
            emit_line(values_slot + " = bitcast i8* " + values_slot_raw + " to i8**");
            const std::string values_raw = next_register(object_name + "_dict_values_raw");
            emit_line(values_raw + " = load i8*, i8** " + values_slot);
            const std::string values_typed = next_register(object_name + "_dict_values_typed");
            emit_line(values_typed + " = bitcast i8* " + values_raw + " to " + value_type + "*");

            const std::string est_body = next_register(object_name + "_dict_est_body");
            emit_line(est_body + " = mul i64 " + len + ", 64");
            const std::string est_total = next_register(object_name + "_dict_est_total");
            emit_line(est_total + " = add i64 " + est_body + ", 4");
            const std::string out_buf = next_register(object_name + "_dict_out");
            emit_line(out_buf + " = call i8* @malloc(i64 " + est_total + ")");

            emit_line("store i8 123, i8* " + out_buf);
            const std::string nul_ptr = next_register(object_name + "_dict_nul_ptr");
            emit_line(nul_ptr + " = getelementptr inbounds i8, i8* " + out_buf + ", i64 1");
            emit_line("store i8 0, i8* " + nul_ptr);

            const std::string idx_ptr = next_register(object_name + "_dict_fmt_idx");
            emit_line(idx_ptr + " = alloca i64");
            emit_line("store i64 0, i64* " + idx_ptr);

            StringConstantInfo comma = get_or_create_string_constant(", ");
            StringConstantInfo colon = get_or_create_string_constant(": ");
            StringConstantInfo close_brace = get_or_create_string_constant("}");
            StringConstantInfo quote = get_or_create_string_constant("\"");

            const std::string cond_label = next_label("dict_fmt_cond");
            const std::string body_label = next_label("dict_fmt_body");
            const std::string first_label = next_label("dict_fmt_first");
            const std::string sep_label = next_label("dict_fmt_sep");
            const std::string end_label = next_label("dict_fmt_end");

            emit_line("br label %" + cond_label);
            emit_label(cond_label);
            const std::string idx = next_register(object_name + "_dict_fmt_i");
            emit_line(idx + " = load i64, i64* " + idx_ptr);
            const std::string cond = next_register(object_name + "_dict_fmt_cmp");
            emit_line(cond + " = icmp slt i64 " + idx + ", " + len);
            emit_line("br i1 " + cond + ", label %" + body_label + ", label %" + end_label);

            emit_label(body_label);
            const std::string is_first = next_register(object_name + "_dict_fmt_first");
            emit_line(is_first + " = icmp eq i64 " + idx + ", 0");
            emit_line("br i1 " + is_first + ", label %" + first_label + ", label %" + sep_label);

            emit_label(sep_label);
            (void)emit_call("i8*", "@strcat",
                            {IRValue{"i8*", out_buf, true}, IRValue{"i8*", string_constant_gep(comma), true}});
            emit_line("br label %" + first_label);

            emit_label(first_label);
            const std::string key_ptr = next_register(object_name + "_dict_fmt_key_ptr");
            emit_line(key_ptr + " = getelementptr inbounds " + key_type + ", " + key_type + "* " +
                      keys_typed + ", i64 " + idx);
            const std::string key_val = next_register(object_name + "_dict_fmt_key_val");
            emit_line(key_val + " = load " + key_type + ", " + key_type + "* " + key_ptr);
            const std::string key_str = emit_scalar_to_string(key_type, key_val, object_name + "_dict_key");
            if (key_type == "i8*") {
                (void)emit_call("i8*", "@strcat",
                                {IRValue{"i8*", out_buf, true}, IRValue{"i8*", string_constant_gep(quote), true}});
            }
            (void)emit_call("i8*", "@strcat", {IRValue{"i8*", out_buf, true}, IRValue{"i8*", key_str, true}});
            if (key_type == "i8*") {
                (void)emit_call("i8*", "@strcat",
                                {IRValue{"i8*", out_buf, true}, IRValue{"i8*", string_constant_gep(quote), true}});
            }
            (void)emit_call("i8*", "@strcat",
                            {IRValue{"i8*", out_buf, true}, IRValue{"i8*", string_constant_gep(colon), true}});

            const std::string value_ptr = next_register(object_name + "_dict_fmt_value_ptr");
            emit_line(value_ptr + " = getelementptr inbounds " + value_type + ", " + value_type + "* " +
                      values_typed + ", i64 " + idx);
            const std::string value_val = next_register(object_name + "_dict_fmt_value_val");
            emit_line(value_val + " = load " + value_type + ", " + value_type + "* " + value_ptr);
            const std::string value_str =
                emit_scalar_to_string(value_type, value_val, object_name + "_dict_value");
            if (value_type == "i8*") {
                (void)emit_call("i8*", "@strcat",
                                {IRValue{"i8*", out_buf, true}, IRValue{"i8*", string_constant_gep(quote), true}});
            }
            (void)emit_call("i8*", "@strcat",
                            {IRValue{"i8*", out_buf, true}, IRValue{"i8*", value_str, true}});
            if (value_type == "i8*") {
                (void)emit_call("i8*", "@strcat",
                                {IRValue{"i8*", out_buf, true}, IRValue{"i8*", string_constant_gep(quote), true}});
            }

            const std::string next_idx = next_register(object_name + "_dict_fmt_next");
            emit_line(next_idx + " = add i64 " + idx + ", 1");
            emit_line("store i64 " + next_idx + ", i64* " + idx_ptr);
            emit_line("br label %" + cond_label);

            emit_label(end_label);
            (void)emit_call("i8*", "@strcat", {IRValue{"i8*", out_buf, true},
                                               IRValue{"i8*", string_constant_gep(close_brace), true}});
            return IRValue{"i8*", out_buf, true};
        };

    auto emit_array_to_string_from_header =
        [this, &emit_scalar_to_string](IRValue header_value,
                                       const std::string& elem_llvm_type,
                                       const std::string& elem_mt_type,
                                       const std::string& prefix) -> IRValue {
            header_value = cast_value(header_value, "i8*");

            const bool elem_is_class =
                !elem_mt_type.empty() && classes.find(elem_mt_type) != classes.end();

            StringConstantInfo empty_const = get_or_create_string_constant("[]");
            StringConstantInfo open_const = get_or_create_string_constant("[");
            StringConstantInfo close_const = get_or_create_string_constant("]");
            StringConstantInfo comma_const = get_or_create_string_constant(", ");
            StringConstantInfo null_const = get_or_create_string_constant("null");
            StringConstantInfo unknown_const = get_or_create_string_constant("<value>");
            StringConstantInfo class_const =
                get_or_create_string_constant(elem_is_class ? ("<" + elem_mt_type + ">") : "<ptr>");

            const std::string result_ptr = next_register(prefix + "_result_ptr");
            emit_line(result_ptr + " = alloca i8*");

            const std::string is_null = next_register(prefix + "_is_null");
            emit_line(is_null + " = icmp eq i8* " + header_value.value + ", null");
            const std::string null_label = next_label(prefix + "_null");
            const std::string build_label = next_label(prefix + "_build");
            const std::string end_label = next_label(prefix + "_end");
            emit_line("br i1 " + is_null + ", label %" + null_label + ", label %" + build_label);

            emit_label(null_label);
            emit_line("store i8* " + string_constant_gep(empty_const) + ", i8** " + result_ptr);
            emit_line("br label %" + end_label);

            emit_label(build_label);
            const std::string len_slot = next_register(prefix + "_len_slot");
            emit_line(len_slot + " = bitcast i8* " + header_value.value + " to i64*");
            const std::string len = next_register(prefix + "_len");
            emit_line(len + " = load i64, i64* " + len_slot);

            const std::string est_body = next_register(prefix + "_est_body");
            emit_line(est_body + " = mul i64 " + len + ", 64");
            const std::string est_total = next_register(prefix + "_est_total");
            emit_line(est_total + " = add i64 " + est_body + ", 8");
            const std::string out_buf = next_register(prefix + "_buf");
            emit_line(out_buf + " = call i8* @malloc(i64 " + est_total + ")");

            (void)emit_call(
                "i8*", "@strcpy",
                {IRValue{"i8*", out_buf, true}, IRValue{"i8*", string_constant_gep(open_const), true}});

            const std::string data_slot_raw = next_register(prefix + "_data_slot_raw");
            emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header_value.value + ", i64 16");
            const std::string data_slot = next_register(prefix + "_data_slot");
            emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");
            const std::string data_raw = next_register(prefix + "_data_raw");
            emit_line(data_raw + " = load i8*, i8** " + data_slot);
            const std::string typed_data = next_register(prefix + "_typed_data");
            emit_line(typed_data + " = bitcast i8* " + data_raw + " to " + elem_llvm_type + "*");

            const std::string idx_ptr = next_register(prefix + "_idx_ptr");
            emit_line(idx_ptr + " = alloca i64");
            emit_line("store i64 0, i64* " + idx_ptr);

            const std::string cond_label = next_label(prefix + "_cond");
            const std::string body_label = next_label(prefix + "_body");
            const std::string first_label = next_label(prefix + "_first");
            const std::string sep_label = next_label(prefix + "_sep");
            const std::string loop_end_label = next_label(prefix + "_loop_end");
            emit_line("br label %" + cond_label);

            emit_label(cond_label);
            const std::string idx = next_register(prefix + "_idx");
            emit_line(idx + " = load i64, i64* " + idx_ptr);
            const std::string cond = next_register(prefix + "_cond_cmp");
            emit_line(cond + " = icmp slt i64 " + idx + ", " + len);
            emit_line("br i1 " + cond + ", label %" + body_label + ", label %" + loop_end_label);

            emit_label(body_label);
            const std::string is_first = next_register(prefix + "_is_first");
            emit_line(is_first + " = icmp eq i64 " + idx + ", 0");
            emit_line("br i1 " + is_first + ", label %" + first_label + ", label %" + sep_label);

            emit_label(sep_label);
            (void)emit_call("i8*", "@strcat",
                            {IRValue{"i8*", out_buf, true}, IRValue{"i8*", string_constant_gep(comma_const), true}});
            emit_line("br label %" + first_label);

            emit_label(first_label);
            const std::string elem_ptr = next_register(prefix + "_elem_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + elem_llvm_type + ", " +
                      elem_llvm_type + "* " + typed_data + ", i64 " + idx);
            const std::string elem_val = next_register(prefix + "_elem_val");
            emit_line(elem_val + " = load " + elem_llvm_type + ", " + elem_llvm_type + "* " + elem_ptr);

            if (elem_is_class) {
                const std::string elem_is_null = next_register(prefix + "_elem_is_null");
                emit_line(elem_is_null + " = icmp eq i8* " + elem_val + ", null");
                const std::string elem_str = next_register(prefix + "_elem_str");
                emit_line(elem_str + " = select i1 " + elem_is_null + ", i8* " +
                          string_constant_gep(null_const) + ", i8* " + string_constant_gep(class_const));
                (void)emit_call("i8*", "@strcat",
                                {IRValue{"i8*", out_buf, true}, IRValue{"i8*", elem_str, true}});
            } else if (elem_llvm_type == "i32" || elem_llvm_type == "i64" ||
                       elem_llvm_type == "double" || elem_llvm_type == "i1" ||
                       elem_llvm_type == "i8*") {
                const std::string elem_str =
                    emit_scalar_to_string(elem_llvm_type, elem_val, prefix + "_elem_str");
                (void)emit_call("i8*", "@strcat",
                                {IRValue{"i8*", out_buf, true}, IRValue{"i8*", elem_str, true}});
            } else {
                (void)emit_call("i8*", "@strcat",
                                {IRValue{"i8*", out_buf, true},
                                 IRValue{"i8*", string_constant_gep(unknown_const), true}});
            }

            const std::string next_idx = next_register(prefix + "_next_idx");
            emit_line(next_idx + " = add i64 " + idx + ", 1");
            emit_line("store i64 " + next_idx + ", i64* " + idx_ptr);
            emit_line("br label %" + cond_label);

            emit_label(loop_end_label);
            (void)emit_call("i8*", "@strcat",
                            {IRValue{"i8*", out_buf, true}, IRValue{"i8*", string_constant_gep(close_const), true}});
            emit_line("store i8* " + out_buf + ", i8** " + result_ptr);
            emit_line("br label %" + end_label);

            emit_label(end_label);
            const std::string result = next_register(prefix + "_result");
            emit_line(result + " = load i8*, i8** " + result_ptr);
            return IRValue{"i8*", result, true};
        };

    if (node.callee && is_node<MemberExpression>(node.callee)) {
        auto& member = get_node<MemberExpression>(node.callee);
        const std::string method_name = member.property;
        const VariableInfo* var = nullptr;
        const CodegenClassFieldInfo* member_object_field = nullptr;
        std::string inferred_object_class;
        std::string object_name;
        if (member.object && is_node<Identifier>(member.object)) {
            object_name = get_node<Identifier>(member.object).name;
            var = resolve_variable(object_name);
        } else if (member.object && is_node<ThisExpression>(member.object)) {
            object_name = "this";
            var = resolve_variable("this");
        } else if (member.object && is_node<MemberExpression>(member.object)) {
            const auto& inner = get_node<MemberExpression>(member.object);
            const VariableInfo* owner_var = nullptr;
            if (inner.object && is_node<Identifier>(inner.object)) {
                owner_var = resolve_variable(get_node<Identifier>(inner.object).name);
            } else if (inner.object && is_node<ThisExpression>(inner.object)) {
                owner_var = resolve_variable("this");
            }
            if (owner_var && !owner_var->class_name.empty()) {
                const auto class_it = classes.find(owner_var->class_name);
                if (class_it != classes.end()) {
                    const auto field_it = class_it->second.fields.find(inner.property);
                    if (field_it != class_it->second.fields.end()) {
                        member_object_field = &field_it->second;
                    }
                }
            }
        }
        if (member.object) {
            inferred_object_class = infer_class_name_from_ast(member.object);
        }

        if (member.object && is_node<Identifier>(member.object) &&
            get_node<Identifier>(member.object).name == "super" && method_name == "new") {
            const VariableInfo* this_var = resolve_variable("this");
            if (!this_var || this_var->class_name.empty()) {
                throw std::runtime_error("super.new() can only be used inside class methods");
            }

            const auto class_it = classes.find(this_var->class_name);
            if (class_it == classes.end()) {
                throw std::runtime_error("Unknown class '" + this_var->class_name +
                                         "' while lowering super.new()");
            }
            if (class_it->second.parent_class.empty()) {
                throw std::runtime_error("Class '" + this_var->class_name +
                                         "' does not extend another class");
            }

            const std::string parent_class = class_it->second.parent_class;
            const auto parent_it = classes.find(parent_class);
            if (parent_it == classes.end()) {
                throw std::runtime_error("Unknown parent class '" + parent_class + "'");
            }

            const auto parent_ctor_it = parent_it->second.methods.find("new");
            if (parent_ctor_it == parent_it->second.methods.end()) {
                if (!node.arguments.empty()) {
                    throw std::runtime_error("Parent constructor '" + parent_class +
                                             ".new' does not exist but arguments were provided");
                }
                return IRValue{"void", "", true};
            }

            const CodegenClassMethodInfo& parent_ctor = parent_ctor_it->second;
            const std::size_t expected_params = parent_ctor.parameters.size() > 0
                ? parent_ctor.parameters.size() - 1 : 0;

            std::vector<ASTNode> arg_nodes;
            arg_nodes.reserve(node.arguments.size());
            for (const auto& arg : node.arguments) {
                arg_nodes.push_back(clone_node(arg));
            }
            while (arg_nodes.size() < expected_params) {
                const std::size_t param_index = arg_nodes.size() + 1;
                if (param_index >= parent_ctor.parameters.size() ||
                    !parent_ctor.parameters[param_index].default_value) {
                    break;
                }
                arg_nodes.push_back(clone_node(parent_ctor.parameters[param_index].default_value));
            }
            if (arg_nodes.size() != expected_params) {
                throw std::runtime_error("Method '" + parent_class + ".new' argument count mismatch");
            }

            const std::string this_reg = next_register("super_this");
            emit_line(this_reg + " = load i8*, i8** " + this_var->ptr_value);

            std::vector<IRValue> args;
            args.reserve(1 + arg_nodes.size());
            args.push_back(IRValue{"i8*", this_reg, true});
            for (std::size_t i = 0; i < arg_nodes.size(); ++i) {
                const auto& param = parent_ctor.parameters[i + 1];
                IRValue arg = cast_value(generate_expression(arg_nodes[i]), param.llvm_type);
                args.push_back(std::move(arg));
            }

            return emit_call(parent_ctor.return_type, "@" + parent_ctor.mangled_name, args);
        }

        // Support module-style calls from `use module as alias`: alias.func(...)
        if (!var && member.object && is_node<Identifier>(member.object)) {
            const auto fn_it = functions.find(method_name);
            if (fn_it != functions.end()) {
                const CodegenFunctionInfo& info = fn_it->second;

                std::vector<ASTNode> arg_nodes;
                arg_nodes.reserve(node.arguments.size());
                for (const auto& arg : node.arguments) {
                    arg_nodes.push_back(clone_node(arg));
                }

                if (!info.is_var_arg) {
                    while (arg_nodes.size() < info.parameters.size() &&
                           info.parameters[arg_nodes.size()].default_value) {
                        arg_nodes.push_back(clone_node(info.parameters[arg_nodes.size()].default_value));
                    }

                    if (arg_nodes.size() != info.parameters.size()) {
                        throw std::runtime_error("Function '" + method_name +
                                                 "' argument count mismatch during codegen");
                    }
                } else if (arg_nodes.size() < info.parameters.size()) {
                    throw std::runtime_error("Function '" + method_name + "' requires at least " +
                                             std::to_string(info.parameters.size()) + " arguments");
                }

                std::vector<IRValue> args;
                args.reserve(arg_nodes.size());
                for (std::size_t i = 0; i < arg_nodes.size(); ++i) {
                    IRValue arg_value = generate_expression(arg_nodes[i]);
                    if (i < info.parameters.size()) {
                        arg_value = cast_value(arg_value, info.parameters[i].llvm_type);
                    } else if (arg_value.type == "i1") {
                        arg_value = cast_value(arg_value, "i32");
                    }
                    args.push_back(std::move(arg_value));
                }

                return emit_call(info.return_type, "@" + method_name, args);
            }
        }

        if (method_name == "length") {
            if (!node.arguments.empty()) {
                throw std::runtime_error("length() method expects no arguments");
            }

            if (var && var->is_fixed_array) {
                return IRValue{"i32", std::to_string(var->fixed_array_size), true};
            }
            if (var && var->is_dynamic_array) {
                return emit_dynamic_array_length(object_name.empty() ? "arr" : object_name, var);
            }
            if (var && var->is_dict) {
                const std::string header = next_register(object_name + "_dict_hdr");
                emit_line(header + " = load i8*, i8** " + var->ptr_value);
                const std::string len_slot = next_register(object_name + "_dict_len_slot");
                emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
                const std::string len_i64 = next_register(object_name + "_dict_len_i64");
                emit_line(len_i64 + " = load i64, i64* " + len_slot);
                const std::string len_i32 = next_register(object_name + "_dict_len_i32");
                emit_line(len_i32 + " = trunc i64 " + len_i64 + " to i32");
                return IRValue{"i32", len_i32, true};
            }
            if (member_object_field && member_object_field->mt_type == "array") {
                IRValue object_value = cast_value(generate_expression(member.object), "i8*");
                const std::string tmp_ptr = next_register("member_obj_addr");
                emit_line(tmp_ptr + " = alloca i8*");
                emit_line("store i8* " + object_value.value + ", i8** " + tmp_ptr);
                VariableInfo temp_var{
                    "i8*",
                    tmp_ptr,
                    false,
                    "",
                    0,
                    true,
                    "i8*",
                    "",
                    false,
                    "",
                    "",
                    "",
                    "",
                };
                return emit_dynamic_array_length(object_name.empty() ? "arr" : object_name, &temp_var);
            }

            IRValue object_value = generate_expression(member.object);
            if (object_value.type == "i8*") {
                return emit_strlen_for_string(object_value);
            }
            throw std::runtime_error("Unsupported object type for .length()");
        }

        if (method_name == "append") {
            if (node.arguments.size() != 1) {
                throw std::runtime_error("append() expects exactly one argument");
            }
            if (!var || !var->is_dynamic_array) {
                if (!(member_object_field && member_object_field->mt_type == "array")) {
                    throw std::runtime_error("append() is only implemented for dynamic arrays");
                }
                IRValue object_value = cast_value(generate_expression(member.object), "i8*");
                const std::string tmp_ptr = next_register("member_obj_addr");
                emit_line(tmp_ptr + " = alloca i8*");
                emit_line("store i8* " + object_value.value + ", i8** " + tmp_ptr);
                VariableInfo temp_var{
                    "i8*",
                    tmp_ptr,
                    false,
                    "",
                    0,
                    true,
                    "i8*",
                    "",
                    false,
                    "",
                    "",
                    "",
                    "",
                };
                return emit_dynamic_array_append(object_name.empty() ? "arr" : object_name, &temp_var,
                                                 node.arguments[0]);
            }
            return emit_dynamic_array_append(object_name.empty() ? "arr" : object_name, var, node.arguments[0]);
        }

        if (method_name == "pop") {
            if (node.arguments.size() > 1) {
                throw std::runtime_error("pop() expects 0 or 1 index argument");
            }
            ASTNode* pop_index = node.arguments.empty() ? nullptr : &node.arguments[0];
            if (!var || !var->is_dynamic_array) {
                if (!(member_object_field && member_object_field->mt_type == "array")) {
                    throw std::runtime_error("pop() is only implemented for dynamic arrays");
                }
                IRValue object_value = cast_value(generate_expression(member.object), "i8*");
                const std::string tmp_ptr = next_register("member_obj_addr");
                emit_line(tmp_ptr + " = alloca i8*");
                emit_line("store i8* " + object_value.value + ", i8** " + tmp_ptr);
                VariableInfo temp_var{
                    "i8*",
                    tmp_ptr,
                    false,
                    "",
                    0,
                    true,
                    "i8*",
                    "",
                    false,
                    "",
                    "",
                    "",
                    "",
                };
                return emit_dynamic_array_pop(object_name.empty() ? "arr" : object_name, &temp_var,
                                              pop_index);
            }
            return emit_dynamic_array_pop(object_name.empty() ? "arr" : object_name, var, pop_index);
        }

        std::string target_class_name;
        if (var && !var->class_name.empty()) {
            target_class_name = var->class_name;
        } else {
            target_class_name = inferred_object_class;
        }

        if (!target_class_name.empty()) {
            const auto class_it = classes.find(target_class_name);
            if (class_it == classes.end()) {
                throw std::runtime_error("Unknown class '" + target_class_name + "'");
            }
            const auto method_it = class_it->second.methods.find(method_name);
            if (method_it == class_it->second.methods.end()) {
                throw std::runtime_error("Unknown method '" + method_name + "' for class '" +
                                         target_class_name + "'");
            }

            const CodegenClassMethodInfo& method_info = method_it->second;
            const std::size_t expected_params = method_info.parameters.size() > 0
                ? method_info.parameters.size() - 1 : 0;

            std::vector<ASTNode> arg_nodes;
            arg_nodes.reserve(node.arguments.size());
            for (const auto& arg : node.arguments) {
                arg_nodes.push_back(clone_node(arg));
            }
            while (arg_nodes.size() < expected_params) {
                const std::size_t param_index = arg_nodes.size() + 1;
                if (param_index >= method_info.parameters.size() ||
                    !method_info.parameters[param_index].default_value) {
                    break;
                }
                arg_nodes.push_back(clone_node(method_info.parameters[param_index].default_value));
            }
            if (arg_nodes.size() != expected_params) {
                throw std::runtime_error("Method '" + target_class_name + "." + method_name +
                                         "' argument count mismatch");
            }

            std::vector<IRValue> args;
            args.reserve(1 + arg_nodes.size());
            IRValue self = generate_expression(member.object);
            self = cast_value(self, "i8*");
            args.push_back(self);

            for (std::size_t i = 0; i < arg_nodes.size(); ++i) {
                const auto& param = method_info.parameters[i + 1];
                IRValue arg = cast_value(generate_expression(arg_nodes[i]), param.llvm_type);
                args.push_back(std::move(arg));
            }

            return emit_call(method_info.return_type, "@" + method_info.mangled_name, args);
        }

        throw std::runtime_error("Unsupported member call '" + method_name + "'");
    }

    std::string callee_name;
    if (node.callee && is_node<Identifier>(node.callee)) {
        callee_name = get_node<Identifier>(node.callee).name;
    } else if (node.callee && is_node<TypeLiteral>(node.callee)) {
        callee_name = get_node<TypeLiteral>(node.callee).name;
    } else {
        throw std::runtime_error("Unsupported callee in call expression");
    }

    if (callee_name == "print") {
        if (node.arguments.empty()) {
            throw std::runtime_error("print() requires one argument");
        }

        IRValue arg;
        bool handled_special = false;
        if (node.arguments[0] && is_node<Identifier>(node.arguments[0])) {
            const std::string object_name = get_node<Identifier>(node.arguments[0]).name;
            const VariableInfo* var = resolve_variable(object_name);
            if (var && var->is_dict) {
                arg = emit_dict_to_string(object_name, var);
                handled_special = true;
            } else if (var && var->is_dynamic_array) {
                const std::string header = next_register(object_name + "_arr_hdr");
                emit_line(header + " = load i8*, i8** " + var->ptr_value);
                arg = emit_array_to_string_from_header(
                    IRValue{"i8*", header, true},
                    var->dynamic_array_elem_llvm_type.empty() ? "i8*" : var->dynamic_array_elem_llvm_type,
                    var->dynamic_array_elem_mt_type,
                    object_name + "_arr_print");
                handled_special = true;
            } else if (var && var->is_fixed_array) {
                StringConstantInfo arr_const = get_or_create_string_constant(
                    "[array len=" + std::to_string(var->fixed_array_size) + "]");
                arg = IRValue{"i8*", string_constant_gep(arr_const), true};
                handled_special = true;
            }
        }
        if (!handled_special && node.arguments[0] && is_node<MemberExpression>(node.arguments[0])) {
            const auto& member_arg = get_node<MemberExpression>(node.arguments[0]);
            const VariableInfo* owner_var = nullptr;
            if (member_arg.object && is_node<Identifier>(member_arg.object)) {
                owner_var = resolve_variable(get_node<Identifier>(member_arg.object).name);
            } else if (member_arg.object && is_node<ThisExpression>(member_arg.object)) {
                owner_var = resolve_variable("this");
            }
            if (owner_var && !owner_var->class_name.empty()) {
                const auto class_it = classes.find(owner_var->class_name);
                if (class_it != classes.end()) {
                    const auto field_it = class_it->second.fields.find(member_arg.property);
                    if (field_it != class_it->second.fields.end() &&
                        field_it->second.mt_type == "array") {
                        IRValue header_value =
                            cast_value(generate_expression(node.arguments[0]), "i8*");
                        std::string elem_mt_type = field_it->second.element_mt_type;
                        if (elem_mt_type.empty()) {
                            elem_mt_type = "any";
                        }
                        std::string elem_llvm_type = map_type_to_llvm(elem_mt_type);
                        if (elem_llvm_type == "void") {
                            elem_llvm_type = "i8*";
                        }
                        arg = emit_array_to_string_from_header(
                            header_value,
                            elem_llvm_type,
                            elem_mt_type,
                            owner_var->class_name + "_" + member_arg.property + "_print");
                        handled_special = true;
                    }
                }
            }
        }
        if (!handled_special) {
            arg = generate_expression(node.arguments[0]);
        }
        std::string fmt = "%d\n";

        if (arg.type == "double") {
            fmt = "%f\n";
        } else if (arg.type == "i8*") {
            fmt = "%s\n";
        } else if (arg.type == "i1") {
            arg = cast_value(arg, "i32");
            fmt = "%d\n";
        } else if (is_pointer_type(arg.type)) {
            arg = cast_value(arg, "i8*");
            fmt = "%s\n";
        } else if (arg.type != "i32") {
            arg = cast_value(arg, "i32");
        }

        StringConstantInfo format_const = get_or_create_string_constant(fmt);
        IRValue fmt_arg{"i8*", string_constant_gep(format_const), true};
        return emit_call("i32", "@printf", {fmt_arg, arg}, true);
    }

    if (callee_name == "str") {
        if (node.arguments.size() != 1) {
            throw std::runtime_error("str() takes exactly one argument");
        }

        bool handled_special = false;
        IRValue arg;
        if (node.arguments[0] && is_node<Identifier>(node.arguments[0])) {
            const std::string object_name = get_node<Identifier>(node.arguments[0]).name;
            const VariableInfo* var = resolve_variable(object_name);
            if (var && var->is_dynamic_array) {
                const std::string header = next_register(object_name + "_arr_hdr");
                emit_line(header + " = load i8*, i8** " + var->ptr_value);
                arg = emit_array_to_string_from_header(
                    IRValue{"i8*", header, true},
                    var->dynamic_array_elem_llvm_type.empty() ? "i8*" : var->dynamic_array_elem_llvm_type,
                    var->dynamic_array_elem_mt_type,
                    object_name + "_arr_str");
                handled_special = true;
            } else if (var && var->is_fixed_array) {
                StringConstantInfo arr_const = get_or_create_string_constant(
                    "[array len=" + std::to_string(var->fixed_array_size) + "]");
                arg = IRValue{"i8*", string_constant_gep(arr_const), true};
                handled_special = true;
            }
        }
        if (!handled_special && node.arguments[0] && is_node<MemberExpression>(node.arguments[0])) {
            const auto& member_arg = get_node<MemberExpression>(node.arguments[0]);
            const VariableInfo* owner_var = nullptr;
            if (member_arg.object && is_node<Identifier>(member_arg.object)) {
                owner_var = resolve_variable(get_node<Identifier>(member_arg.object).name);
            } else if (member_arg.object && is_node<ThisExpression>(member_arg.object)) {
                owner_var = resolve_variable("this");
            }
            if (owner_var && !owner_var->class_name.empty()) {
                const auto class_it = classes.find(owner_var->class_name);
                if (class_it != classes.end()) {
                    const auto field_it = class_it->second.fields.find(member_arg.property);
                    if (field_it != class_it->second.fields.end() &&
                        field_it->second.mt_type == "array") {
                        std::string elem_mt_type = field_it->second.element_mt_type;
                        if (elem_mt_type.empty()) {
                            elem_mt_type = "any";
                        }
                        std::string elem_llvm_type = map_type_to_llvm(elem_mt_type);
                        if (elem_llvm_type == "void") {
                            elem_llvm_type = "i8*";
                        }
                        IRValue header_value =
                            cast_value(generate_expression(node.arguments[0]), "i8*");
                        arg = emit_array_to_string_from_header(
                            header_value,
                            elem_llvm_type,
                            elem_mt_type,
                            owner_var->class_name + "_" + member_arg.property + "_str");
                        handled_special = true;
                    }
                }
            }
        }

        if (!handled_special) {
            arg = generate_expression(node.arguments[0]);
        }
        if (arg.type == "i8*") {
            return arg;
        }

        IRValue size{"i64", "64", true};
        IRValue buffer = emit_call("i8*", "@malloc", {size});
        std::string fmt = "%d";

        if (arg.type == "double") {
            fmt = "%g";
        } else if (arg.type == "i1") {
            arg = cast_value(arg, "i32");
        } else if (arg.type != "i32") {
            arg = cast_value(arg, "i32");
        }

        StringConstantInfo fmt_const = get_or_create_string_constant(fmt);
        IRValue fmt_ptr{"i8*", string_constant_gep(fmt_const), true};
        (void)emit_call("i32", "@sprintf", {buffer, fmt_ptr, arg}, true);
        return buffer;
    }

    if (callee_name == "int") {
        if (node.arguments.size() != 1) {
            throw std::runtime_error("int() takes exactly one argument");
        }
        IRValue arg = generate_expression(node.arguments[0]);
        if (arg.type == "i32") {
            return arg;
        }
        if (arg.type == "double") {
            return cast_value(arg, "i32");
        }
        if (arg.type == "i1") {
            return cast_value(arg, "i32");
        }
        if (arg.type == "i8*") {
            const std::string end_ptr_addr = next_register("int_endptr_addr");
            emit_line(end_ptr_addr + " = alloca i8*");
            emit_line("store i8* null, i8** " + end_ptr_addr);

            const std::string parsed_i64 = next_register("int_parsed_i64");
            emit_line(parsed_i64 + " = call i64 @strtol(i8* " + arg.value + ", i8** " + end_ptr_addr +
                      ", i32 10)");
            const std::string end_ptr = next_register("int_endptr");
            emit_line(end_ptr + " = load i8*, i8** " + end_ptr_addr);

            const std::string consumed_any = next_register("int_consumed_any");
            emit_line(consumed_any + " = icmp ne i8* " + end_ptr + ", " + arg.value);
            const std::string end_char = next_register("int_end_char");
            emit_line(end_char + " = load i8, i8* " + end_ptr);
            const std::string ended_clean = next_register("int_ended_clean");
            emit_line(ended_clean + " = icmp eq i8 " + end_char + ", 0");
            const std::string is_valid = next_register("int_is_valid");
            emit_line(is_valid + " = and i1 " + consumed_any + ", " + ended_clean);

            const std::string valid_label = next_label("int_valid");
            const std::string invalid_label = next_label("int_invalid");
            const std::string merge_label = next_label("int_merge");
            emit_line("br i1 " + is_valid + ", label %" + valid_label + ", label %" + invalid_label);

            emit_label(valid_label);
            const std::string parsed_i32 = next_register("int_parsed_i32");
            emit_line(parsed_i32 + " = trunc i64 " + parsed_i64 + " to i32");
            emit_line("br label %" + merge_label);

            emit_label(invalid_label);
            emit_line("store i8* null, i8** @__mt_exc_obj");
            emit_line("store i32 " + std::to_string(class_tag_for_name("ValueError")) +
                      ", i32* @__mt_exc_tag");

            const std::string jmp_ptr = next_register("int_throw_jmp_ptr");
            emit_line(jmp_ptr + " = load i8*, i8** @__mt_exc_jmp");
            const std::string has_jmp = next_register("int_throw_has_jmp");
            emit_line(has_jmp + " = icmp ne i8* " + jmp_ptr + ", null");
            const std::string longjmp_label = next_label("int_throw_longjmp");
            const std::string exit_label = next_label("int_throw_exit");
            emit_line("br i1 " + has_jmp + ", label %" + longjmp_label + ", label %" + exit_label);

            emit_label(longjmp_label);
            emit_line("call void @longjmp(i8* " + jmp_ptr + ", i32 1)");
            emit_line("unreachable");

            emit_label(exit_label);
            StringConstantInfo msg = get_or_create_string_constant("Invalid int conversion");
            IRValue msg_arg{"i8*", string_constant_gep(msg), true};
            IRValue code_arg{"i32", "1002", true};
            (void)emit_call("void", "@__mt_runtime_panic", {msg_arg, code_arg}, true);
            emit_line("unreachable");

            emit_label(merge_label);
            return IRValue{"i32", parsed_i32, true};
        }
        return cast_value(arg, "i32");
    }

    if (callee_name == "float") {
        if (node.arguments.size() != 1) {
            throw std::runtime_error("float() takes exactly one argument");
        }
        IRValue arg = generate_expression(node.arguments[0]);
        if (arg.type == "double") {
            return arg;
        }
        if (arg.type == "i32" || arg.type == "i1") {
            return cast_value(arg, "double");
        }
        if (arg.type == "i8*") {
            return emit_call("double", "@atof", {arg});
        }
        return cast_value(arg, "double");
    }

    if (callee_name == "read") {
        if (node.arguments.size() > 1) {
            throw std::runtime_error("read() takes 0 or 1 arguments");
        }

        if (node.arguments.size() == 1) {
            IRValue prompt = generate_expression(node.arguments[0]);
            std::string fmt = "%s";
            if (prompt.type == "double") {
                fmt = "%f";
            } else if (prompt.type == "i32" || prompt.type == "i1") {
                prompt = cast_value(prompt, "i32");
                fmt = "%d";
            } else {
                prompt = cast_value(prompt, "i8*");
            }

            StringConstantInfo fmt_const = get_or_create_string_constant(fmt);
            IRValue fmt_ptr{"i8*", string_constant_gep(fmt_const), true};
            (void)emit_call("i32", "@printf", {fmt_ptr, prompt}, true);
        }

        IRValue buffer_size{"i64", "1024", true};
        IRValue buffer = emit_call("i8*", "@malloc", {buffer_size});

        const std::string stdin_stream = next_register("stdin_stream");
        emit_line(stdin_stream + " = load i8*, i8** @stdin");
        IRValue stdin_arg{"i8*", stdin_stream, true};
        IRValue fgets_size{"i32", "1024", true};
        (void)emit_call("i8*", "@fgets", {buffer, fgets_size, stdin_arg});

        IRValue len = emit_call("i64", "@strlen", {buffer});
        const std::string has_chars = next_register("read_has_chars");
        emit_line(has_chars + " = icmp sgt i64 " + len.value + ", 0");

        const std::string check_label = next_label("read_check_nl");
        const std::string strip_label = next_label("read_strip_nl");
        const std::string keep_label = next_label("read_keep_nl");
        const std::string done_label = next_label("read_done");

        emit_line("br i1 " + has_chars + ", label %" + check_label + ", label %" + done_label);

        emit_label(check_label);
        const std::string last_idx = next_register("read_last_idx");
        emit_line(last_idx + " = sub i64 " + len.value + ", 1");
        const std::string nl_ptr = next_register("read_nl_ptr");
        emit_line(nl_ptr + " = getelementptr inbounds i8, i8* " + buffer.value + ", i64 " + last_idx);
        const std::string last_char = next_register("read_last_char");
        emit_line(last_char + " = load i8, i8* " + nl_ptr);
        const std::string is_newline = next_register("read_is_newline");
        emit_line(is_newline + " = icmp eq i8 " + last_char + ", 10");
        emit_line("br i1 " + is_newline + ", label %" + strip_label + ", label %" + keep_label);

        emit_label(strip_label);
        emit_line("store i8 0, i8* " + nl_ptr);
        emit_line("br label %" + done_label);

        emit_label(keep_label);
        emit_line("br label %" + done_label);

        emit_label(done_label);
        return buffer;
    }

    if (callee_name == "split") {
        if (node.arguments.size() != 2) {
            throw std::runtime_error("split() expects exactly two arguments");
        }

        IRValue text = cast_value(generate_expression(node.arguments[0]), "i8*");
        IRValue sep = cast_value(generate_expression(node.arguments[1]), "i8*");
        IRValue text_len = emit_call("i64", "@strlen", {text});
        IRValue sep_len = emit_call("i64", "@strlen", {sep});

        const std::string sep_is_empty = next_register("split_sep_is_empty");
        emit_line(sep_is_empty + " = icmp eq i64 " + sep_len.value + ", 0");
        const std::string error_label = next_label("split_error");
        const std::string count_init_label = next_label("split_count_init");
        emit_line("br i1 " + sep_is_empty + ", label %" + error_label + ", label %" + count_init_label);

        emit_label(error_label);
        StringConstantInfo err_msg = get_or_create_string_constant("split() separator must not be empty");
        IRValue err_msg_arg{"i8*", string_constant_gep(err_msg), true};
        IRValue err_code_arg{"i32", "1002", true};
        (void)emit_call("void", "@__mt_runtime_panic", {err_msg_arg, err_code_arg}, true);
        emit_line("unreachable");

        emit_label(count_init_label);
        const std::string text_bytes = next_register("split_text_bytes");
        emit_line(text_bytes + " = add i64 " + text_len.value + ", 1");

        const std::string work_count = next_register("split_work_count");
        emit_line(work_count + " = call i8* @malloc(i64 " + text_bytes + ")");
        (void)emit_call("i8*", "@strcpy", {IRValue{"i8*", work_count, true}, text}, true);

        const std::string work_store = next_register("split_work_store");
        emit_line(work_store + " = call i8* @malloc(i64 " + text_bytes + ")");
        (void)emit_call("i8*", "@strcpy", {IRValue{"i8*", work_store, true}, text}, true);

        const std::string count_slot = next_register("split_count_slot");
        emit_line(count_slot + " = alloca i64");
        emit_line("store i64 0, i64* " + count_slot);
        const std::string scan_ptr_slot = next_register("split_scan_ptr_slot");
        emit_line(scan_ptr_slot + " = alloca i8*");
        emit_line("store i8* " + work_count + ", i8** " + scan_ptr_slot);

        const std::string count_cond_label = next_label("split_count_cond");
        const std::string count_body_label = next_label("split_count_body");
        const std::string count_done_label = next_label("split_count_done");
        emit_line("br label %" + count_cond_label);

        emit_label(count_cond_label);
        const std::string scan_ptr = next_register("split_scan_ptr");
        emit_line(scan_ptr + " = load i8*, i8** " + scan_ptr_slot);
        const std::string found = next_register("split_found");
        emit_line(found + " = call i8* @strstr(i8* " + scan_ptr + ", i8* " + sep.value + ")");
        const std::string has_found = next_register("split_has_found");
        emit_line(has_found + " = icmp ne i8* " + found + ", null");
        emit_line("br i1 " + has_found + ", label %" + count_body_label + ", label %" + count_done_label);

        emit_label(count_body_label);
        const std::string cur_count = next_register("split_cur_count");
        emit_line(cur_count + " = load i64, i64* " + count_slot);
        const std::string next_count = next_register("split_next_count");
        emit_line(next_count + " = add i64 " + cur_count + ", 1");
        emit_line("store i64 " + next_count + ", i64* " + count_slot);
        const std::string next_scan_ptr = next_register("split_next_scan_ptr");
        emit_line(next_scan_ptr + " = getelementptr inbounds i8, i8* " + found + ", i64 " + sep_len.value);
        emit_line("store i8* " + next_scan_ptr + ", i8** " + scan_ptr_slot);
        emit_line("br label %" + count_cond_label);

        emit_label(count_done_label);
        const std::string split_count = next_register("split_count");
        emit_line(split_count + " = load i64, i64* " + count_slot);
        const std::string token_count = next_register("split_token_count");
        emit_line(token_count + " = add i64 " + split_count + ", 1");

        const std::string header_raw = next_register("split_arr_hdr_raw");
        emit_line(header_raw + " = call i8* @malloc(i64 24)");
        const std::string len_slot = next_register("split_arr_len_slot");
        emit_line(len_slot + " = bitcast i8* " + header_raw + " to i64*");
        emit_line("store i64 " + token_count + ", i64* " + len_slot);
        const std::string cap_slot = next_register("split_arr_cap_slot");
        emit_line(cap_slot + " = getelementptr inbounds i64, i64* " + len_slot + ", i64 1");
        emit_line("store i64 " + token_count + ", i64* " + cap_slot);

        const std::string data_slot_raw = next_register("split_arr_data_slot_raw");
        emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header_raw + ", i64 16");
        const std::string data_slot = next_register("split_arr_data_slot");
        emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");
        const std::string data_bytes = next_register("split_data_bytes");
        emit_line(data_bytes + " = mul i64 " + token_count + ", 8");
        const std::string data_raw = next_register("split_arr_data_raw");
        emit_line(data_raw + " = call i8* @malloc(i64 " + data_bytes + ")");
        emit_line("store i8* " + data_raw + ", i8** " + data_slot);
        const std::string typed_data = next_register("split_arr_typed_data");
        emit_line(typed_data + " = bitcast i8* " + data_raw + " to i8**");

        const std::string idx_slot = next_register("split_idx_slot");
        emit_line(idx_slot + " = alloca i64");
        emit_line("store i64 0, i64* " + idx_slot);
        const std::string cursor_slot = next_register("split_cursor_slot");
        emit_line(cursor_slot + " = alloca i8*");
        emit_line("store i8* " + work_store + ", i8** " + cursor_slot);

        const std::string fill_cond_label = next_label("split_fill_cond");
        const std::string fill_body_label = next_label("split_fill_body");
        const std::string fill_last_label = next_label("split_fill_last");
        const std::string fill_done_label = next_label("split_fill_done");
        emit_line("br label %" + fill_cond_label);

        emit_label(fill_cond_label);
        const std::string cursor = next_register("split_cursor");
        emit_line(cursor + " = load i8*, i8** " + cursor_slot);
        const std::string fill_found = next_register("split_fill_found");
        emit_line(fill_found + " = call i8* @strstr(i8* " + cursor + ", i8* " + sep.value + ")");
        const std::string fill_has_found = next_register("split_fill_has_found");
        emit_line(fill_has_found + " = icmp ne i8* " + fill_found + ", null");
        emit_line("br i1 " + fill_has_found + ", label %" + fill_body_label + ", label %" + fill_last_label);

        emit_label(fill_body_label);
        emit_line("store i8 0, i8* " + fill_found);
        const std::string part_len = next_register("split_part_len");
        emit_line(part_len + " = call i64 @strlen(i8* " + cursor + ")");
        const std::string part_bytes = next_register("split_part_bytes");
        emit_line(part_bytes + " = add i64 " + part_len + ", 1");
        const std::string part_copy = next_register("split_part_copy");
        emit_line(part_copy + " = call i8* @malloc(i64 " + part_bytes + ")");
        (void)emit_call("i8*", "@strcpy", {IRValue{"i8*", part_copy, true}, IRValue{"i8*", cursor, true}}, true);

        const std::string idx = next_register("split_idx");
        emit_line(idx + " = load i64, i64* " + idx_slot);
        const std::string elem_ptr = next_register("split_elem_ptr");
        emit_line(elem_ptr + " = getelementptr inbounds i8*, i8** " + typed_data + ", i64 " + idx);
        emit_line("store i8* " + part_copy + ", i8** " + elem_ptr);
        const std::string idx_next = next_register("split_idx_next");
        emit_line(idx_next + " = add i64 " + idx + ", 1");
        emit_line("store i64 " + idx_next + ", i64* " + idx_slot);
        const std::string cursor_next = next_register("split_cursor_next");
        emit_line(cursor_next + " = getelementptr inbounds i8, i8* " + fill_found + ", i64 " + sep_len.value);
        emit_line("store i8* " + cursor_next + ", i8** " + cursor_slot);
        emit_line("br label %" + fill_cond_label);

        emit_label(fill_last_label);
        const std::string last_len = next_register("split_last_len");
        emit_line(last_len + " = call i64 @strlen(i8* " + cursor + ")");
        const std::string last_bytes = next_register("split_last_bytes");
        emit_line(last_bytes + " = add i64 " + last_len + ", 1");
        const std::string last_copy = next_register("split_last_copy");
        emit_line(last_copy + " = call i8* @malloc(i64 " + last_bytes + ")");
        (void)emit_call("i8*", "@strcpy", {IRValue{"i8*", last_copy, true}, IRValue{"i8*", cursor, true}}, true);
        const std::string last_idx = next_register("split_last_idx");
        emit_line(last_idx + " = load i64, i64* " + idx_slot);
        const std::string last_elem_ptr = next_register("split_last_elem_ptr");
        emit_line(last_elem_ptr + " = getelementptr inbounds i8*, i8** " + typed_data + ", i64 " + last_idx);
        emit_line("store i8* " + last_copy + ", i8** " + last_elem_ptr);
        emit_line("br label %" + fill_done_label);

        emit_label(fill_done_label);
        return IRValue{"i8*", header_raw, true};
    }

    if (callee_name == "args") {
        if (node.arguments.size() != 0) {
            throw std::runtime_error("args() expects no arguments");
        }

        // Load argc and argv from globals
        const std::string argc_i32 = next_register("args_argc_i32");
        emit_line(argc_i32 + " = load i32, i32* @__mt_argc");
        const std::string argc = next_register("args_argc");
        emit_line(argc + " = sext i32 " + argc_i32 + " to i64");
        const std::string argv = next_register("args_argv");
        emit_line(argv + " = load i8**, i8*** @__mt_argv");

        // Allocate array header: [len:i64][cap:i64][data_ptr:i8*] = 24 bytes
        const std::string header = next_register("args_hdr");
        emit_line(header + " = call i8* @malloc(i64 24)");
        const std::string len_slot = next_register("args_len_slot");
        emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
        emit_line("store i64 " + argc + ", i64* " + len_slot);
        const std::string cap_slot = next_register("args_cap_slot");
        emit_line(cap_slot + " = getelementptr inbounds i64, i64* " + len_slot + ", i64 1");
        emit_line("store i64 " + argc + ", i64* " + cap_slot);

        // Allocate data buffer (argc * 8 bytes for i8* pointers)
        const std::string data_slot_raw = next_register("args_data_slot_raw");
        emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 16");
        const std::string data_slot = next_register("args_data_slot");
        emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");
        const std::string data_bytes = next_register("args_data_bytes");
        emit_line(data_bytes + " = mul i64 " + argc + ", 8");
        const std::string data_buf = next_register("args_data_buf");
        emit_line(data_buf + " = call i8* @malloc(i64 " + data_bytes + ")");
        emit_line("store i8* " + data_buf + ", i8** " + data_slot);
        const std::string typed_data = next_register("args_typed_data");
        emit_line(typed_data + " = bitcast i8* " + data_buf + " to i8**");

        // Loop: copy argv pointers into the array data buffer
        const std::string idx_slot = next_register("args_idx_slot");
        emit_line(idx_slot + " = alloca i64");
        emit_line("store i64 0, i64* " + idx_slot);

        const std::string cond_label = next_label("args_cond");
        const std::string body_label = next_label("args_body");
        const std::string done_label = next_label("args_done");
        emit_line("br label %" + cond_label);

        emit_label(cond_label);
        const std::string idx = next_register("args_idx");
        emit_line(idx + " = load i64, i64* " + idx_slot);
        const std::string cmp = next_register("args_cmp");
        emit_line(cmp + " = icmp slt i64 " + idx + ", " + argc);
        emit_line("br i1 " + cmp + ", label %" + body_label + ", label %" + done_label);

        emit_label(body_label);
        const std::string argv_elem_ptr = next_register("args_argv_elem_ptr");
        emit_line(argv_elem_ptr + " = getelementptr inbounds i8*, i8** " + argv + ", i64 " + idx);
        const std::string argv_elem = next_register("args_argv_elem");
        emit_line(argv_elem + " = load i8*, i8** " + argv_elem_ptr);
        const std::string data_elem_ptr = next_register("args_data_elem_ptr");
        emit_line(data_elem_ptr + " = getelementptr inbounds i8*, i8** " + typed_data + ", i64 " + idx);
        emit_line("store i8* " + argv_elem + ", i8** " + data_elem_ptr);
        const std::string idx_next = next_register("args_idx_next");
        emit_line(idx_next + " = add i64 " + idx + ", 1");
        emit_line("store i64 " + idx_next + ", i64* " + idx_slot);
        emit_line("br label %" + cond_label);

        emit_label(done_label);
        return IRValue{"i8*", header, true};
    }

    if (callee_name == "length") {
        if (node.arguments.size() != 1) {
            throw std::runtime_error("length() expects exactly one argument");
        }

        if (node.arguments[0] && is_node<Identifier>(node.arguments[0])) {
            const std::string object_name = get_node<Identifier>(node.arguments[0]).name;
            const VariableInfo* var = resolve_variable(object_name);
            if (var && var->is_fixed_array) {
                return IRValue{"i32", std::to_string(var->fixed_array_size), true};
            }
            if (var && var->is_dynamic_array) {
                return emit_dynamic_array_length(object_name, var);
            }
            if (var && var->is_dict) {
                const std::string header = next_register(object_name + "_dict_hdr");
                emit_line(header + " = load i8*, i8** " + var->ptr_value);
                const std::string len_slot = next_register(object_name + "_dict_len_slot");
                emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
                const std::string len_i64 = next_register(object_name + "_dict_len_i64");
                emit_line(len_i64 + " = load i64, i64* " + len_slot);
                const std::string len_i32 = next_register(object_name + "_dict_len_i32");
                emit_line(len_i32 + " = trunc i64 " + len_i64 + " to i32");
                return IRValue{"i32", len_i32, true};
            }
        }

        IRValue arg = generate_expression(node.arguments[0]);
        if (arg.type == "i8*") {
            return emit_strlen_for_string(arg);
        }
        throw std::runtime_error("length() supports strings, arrays, and dicts");
    }

    if (callee_name == "append") {
        if (node.arguments.size() != 2) {
            throw std::runtime_error("append() expects exactly two arguments");
        }
        if (!(node.arguments[0] && is_node<Identifier>(node.arguments[0]))) {
            throw std::runtime_error("append() expects identifier as first argument");
        }
        const std::string object_name = get_node<Identifier>(node.arguments[0]).name;
        const VariableInfo* var = resolve_variable(object_name);
        if (!var || !var->is_dynamic_array) {
            throw std::runtime_error("append() first argument must be a dynamic array");
        }
        return emit_dynamic_array_append(object_name, var, node.arguments[1]);
    }

    if (callee_name == "pop") {
        if (node.arguments.size() < 1 || node.arguments.size() > 2) {
            throw std::runtime_error("pop() expects 1 or 2 arguments");
        }
        if (!(node.arguments[0] && is_node<Identifier>(node.arguments[0]))) {
            throw std::runtime_error("pop() expects identifier as argument");
        }
        const std::string object_name = get_node<Identifier>(node.arguments[0]).name;
        const VariableInfo* var = resolve_variable(object_name);
        if (!var || !var->is_dynamic_array) {
            throw std::runtime_error("pop() argument must be a dynamic array");
        }
        ASTNode* pop_index = node.arguments.size() == 2 ? &node.arguments[1] : nullptr;
        return emit_dynamic_array_pop(object_name, var, pop_index);
    }

    const auto function_it = functions.find(callee_name);
    if (function_it == functions.end()) {
        throw std::runtime_error("Unknown function '" + callee_name + "' in codegen");
    }

    const CodegenFunctionInfo& info = function_it->second;

    std::vector<ASTNode> arg_nodes;
    arg_nodes.reserve(node.arguments.size());
    for (const auto& arg : node.arguments) {
        arg_nodes.push_back(clone_node(arg));
    }

    if (!info.is_var_arg) {
        while (arg_nodes.size() < info.parameters.size() &&
               info.parameters[arg_nodes.size()].default_value) {
            arg_nodes.push_back(clone_node(info.parameters[arg_nodes.size()].default_value));
        }

        if (arg_nodes.size() != info.parameters.size()) {
            throw std::runtime_error("Function '" + callee_name + "' argument count mismatch during codegen");
        }
    } else if (arg_nodes.size() < info.parameters.size()) {
        throw std::runtime_error("Function '" + callee_name + "' requires at least " +
                                 std::to_string(info.parameters.size()) + " arguments");
    }

    std::vector<IRValue> args;
    args.reserve(arg_nodes.size());
    for (std::size_t i = 0; i < arg_nodes.size(); ++i) {
        IRValue arg_value = generate_expression(arg_nodes[i]);
        if (i < info.parameters.size()) {
            arg_value = cast_value(arg_value, info.parameters[i].llvm_type);
        } else if (arg_value.type == "i1") {
            arg_value = cast_value(arg_value, "i32");
        }
        args.push_back(std::move(arg_value));
    }

    return emit_call(info.return_type, "@" + callee_name, args);
}

CodeGenerator::IRValue CodeGenerator::generate_typeof_expression(TypeofExpression& node) {
    IRValue arg = generate_expression(node.argument);
    StringConstantInfo info = get_or_create_string_constant(infer_type_name(arg));
    return IRValue{"i8*", string_constant_gep(info), true};
}

CodeGenerator::IRValue CodeGenerator::generate_hasattr_expression(HasattrExpression& node) {
    (void)node;
    return IRValue{"i1", node.compile_time_result ? "1" : "0", true};
}

void CodeGenerator::generate_variable_declaration(VariableDeclaration& node) {
    if (current_function_name == "main" && is_top_level_variable_name(node.name) &&
        node.type != "array" && node.type != "dict") {
        const auto global_it = module_globals.find(node.name);
        if (global_it != module_globals.end()) {
            const VariableInfo& global_var = global_it->second;
            IRValue init;
            if (node.value) {
                init = cast_value(generate_expression(node.value), global_var.llvm_type);
            } else if (global_var.llvm_type == "i1") {
                init = IRValue{"i1", "0", true};
            } else if (global_var.llvm_type == "double") {
                init = IRValue{"double", "0.0", true};
            } else if (is_pointer_type(global_var.llvm_type)) {
                init = IRValue{global_var.llvm_type, "null", true};
            } else {
                init = IRValue{global_var.llvm_type, "0", true};
            }
            emit_line("store " + global_var.llvm_type + " " + init.value + ", " +
                      global_var.llvm_type + "* " + global_var.ptr_value);
            return;
        }
    }

    // Check if this variable has a module global (for arrays/dicts in top-level scope)
    const bool has_global = current_function_name == "main" &&
        module_globals.find(node.name) != module_globals.end();

    if (node.type == "array" &&
        !node.is_dynamic &&
        !has_global &&
        node.fixed_size <= 0 &&
        node.value &&
        is_node<ArrayLiteral>(node.value)) {
        auto& literal = get_node<ArrayLiteral>(node.value);
        if (!literal.elements.empty()) {
            std::string element_type = node.element_type;
            if (element_type.empty()) {
                ASTNode& first = literal.elements[0];
                if (is_node<NumberLiteral>(first)) {
                    element_type = "int";
                } else if (is_node<FloatLiteral>(first)) {
                    element_type = "float";
                } else if (is_node<StringLiteral>(first)) {
                    element_type = "string";
                } else if (is_node<BoolLiteral>(first)) {
                    element_type = "bool";
                } else {
                    element_type = "any";
                }
            }

            const std::string elem_llvm_type = map_type_to_llvm(element_type);
            if (elem_llvm_type != "void") {
                const int fixed_size = static_cast<int>(literal.elements.size());
                const std::string array_type = "[" + std::to_string(fixed_size) + " x " +
                                               elem_llvm_type + "]";
                const std::string ptr = next_register(node.name + "_addr");
                emit_line(ptr + " = alloca " + array_type);
                emit_line("store " + array_type + " zeroinitializer, " + array_type + "* " + ptr);
                declare_variable(node.name, VariableInfo{
                    array_type,
                    ptr,
                    true,
                    elem_llvm_type,
                    fixed_size,
                    false,
                    "",
                    "",
                    false,
                    "",
                    "",
                    "",
                    "",
                });

                for (std::size_t i = 0; i < literal.elements.size(); ++i) {
                    IRValue element_value = cast_value(generate_expression(literal.elements[i]), elem_llvm_type);
                    const std::string elem_ptr = next_register(node.name + "_elem_ptr");
                    emit_line(elem_ptr + " = getelementptr inbounds " + array_type + ", " +
                              array_type + "* " + ptr + ", i64 0, i64 " + std::to_string(i));
                    emit_line("store " + elem_llvm_type + " " + element_value.value + ", " +
                              elem_llvm_type + "* " + elem_ptr);
                }
                return;
            }
        }
    }

    if (node.type == "array" && node.fixed_size > 0) {
        if (node.element_type.empty()) {
            throw std::runtime_error("Fixed-size array declaration requires an element type");
        }

        const std::string elem_llvm_type = map_type_to_llvm(node.element_type);
        if (elem_llvm_type == "void") {
            throw std::runtime_error("Fixed-size array element type cannot be void");
        }

        const std::string array_type = "[" + std::to_string(node.fixed_size) + " x " +
                                       elem_llvm_type + "]";
        const std::string ptr = next_register(node.name + "_addr");
        emit_line(ptr + " = alloca " + array_type);
        emit_line("store " + array_type + " zeroinitializer, " + array_type + "* " + ptr);
        declare_variable(node.name, VariableInfo{
            array_type,
            ptr,
            true,
            elem_llvm_type,
            node.fixed_size,
            false,
            "",
            "",
            false,
            "",
            "",
            "",
            "",
        });

        if (node.value) {
            if (!is_node<ArrayLiteral>(node.value)) {
                throw std::runtime_error(
                    "Fixed-size arrays must be initialized with an array literal");
            }

            auto& literal = get_node<ArrayLiteral>(node.value);
            if (literal.elements.size() > static_cast<std::size_t>(node.fixed_size)) {
                throw std::runtime_error("Array initializer has more elements than fixed size");
            }

            for (std::size_t i = 0; i < literal.elements.size(); ++i) {
                IRValue element_value = cast_value(generate_expression(literal.elements[i]), elem_llvm_type);
                const std::string elem_ptr = next_register(node.name + "_elem_ptr");
                emit_line(elem_ptr + " = getelementptr inbounds " + array_type + ", " +
                          array_type + "* " + ptr + ", i64 0, i64 " + std::to_string(i));
                emit_line("store " + elem_llvm_type + " " + element_value.value + ", " +
                          elem_llvm_type + "* " + elem_ptr);
            }
        }

        return;
    }

    if (node.type == "array") {
        if (node.value && !is_node<ArrayLiteral>(node.value)) {
            IRValue existing = cast_value(generate_expression(node.value), "i8*");
            std::string inferred_element_type = node.element_type;
            if (inferred_element_type.empty()) {
                if (is_node<CallExpression>(node.value)) {
                    const auto& call = get_node<CallExpression>(node.value);
                    if (call.callee && is_node<Identifier>(call.callee) &&
                        get_node<Identifier>(call.callee).name == "split") {
                        inferred_element_type = "string";
                    }
                }
            }
            if (inferred_element_type.empty()) {
                inferred_element_type = "any";
            }

            std::string ptr;
            if (has_global) {
                ptr = module_globals.at(node.name).ptr_value;
            } else {
                ptr = next_register(node.name + "_addr");
                emit_line(ptr + " = alloca i8*");
            }
            emit_line("store i8* " + existing.value + ", i8** " + ptr);
            declare_variable(node.name, VariableInfo{
                "i8*",
                ptr,
                false,
                "",
                0,
                true,
                map_type_to_llvm(inferred_element_type),
                "",
                false,
                "",
                "",
                "",
                "",
                inferred_element_type,
            });
            return;
        }

        std::string element_type = node.element_type;
        std::size_t initial_len = 0;

        if (node.value) {
            auto& literal = get_node<ArrayLiteral>(node.value);
            initial_len = literal.elements.size();

            if (element_type.empty() && !literal.elements.empty()) {
                ASTNode& first = literal.elements[0];
                if (is_node<NumberLiteral>(first)) {
                    element_type = "int";
                } else if (is_node<FloatLiteral>(first)) {
                    element_type = "float";
                } else if (is_node<StringLiteral>(first)) {
                    element_type = "string";
                } else if (is_node<BoolLiteral>(first)) {
                    element_type = "bool";
                } else {
                    element_type = "any";
                }
            }
        }

        if (element_type.empty()) {
            element_type = "int";
        }

        const std::string elem_llvm_type = map_type_to_llvm(element_type);
        if (elem_llvm_type == "void") {
            throw std::runtime_error("Dynamic array element type cannot be void");
        }

        std::size_t elem_size = 8;
        if (elem_llvm_type == "i1") {
            elem_size = 1;
        } else if (elem_llvm_type == "i32") {
            elem_size = 4;
        } else if (elem_llvm_type == "double") {
            elem_size = 8;
        } else if (is_pointer_type(elem_llvm_type)) {
            elem_size = 8;
        }

        const std::size_t capacity = std::max<std::size_t>(4, initial_len);
        const std::size_t total_bytes = capacity * elem_size;

        const std::string header_raw = next_register(node.name + "_arr_hdr_raw");
        emit_line(header_raw + " = call i8* @malloc(i64 24)");

        const std::string len_slot = next_register(node.name + "_arr_len_slot");
        emit_line(len_slot + " = bitcast i8* " + header_raw + " to i64*");
        emit_line("store i64 " + std::to_string(initial_len) + ", i64* " + len_slot);

        const std::string cap_slot = next_register(node.name + "_arr_cap_slot");
        emit_line(cap_slot + " = getelementptr inbounds i64, i64* " + len_slot + ", i64 1");
        emit_line("store i64 " + std::to_string(capacity) + ", i64* " + cap_slot);

        const std::string data_slot_raw = next_register(node.name + "_arr_data_slot_raw");
        emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header_raw + ", i64 16");
        const std::string data_slot = next_register(node.name + "_arr_data_slot");
        emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");

        const std::string data_raw = next_register(node.name + "_arr_data_raw");
        emit_line(data_raw + " = call i8* @malloc(i64 " + std::to_string(total_bytes) + ")");
        emit_line("store i8* " + data_raw + ", i8** " + data_slot);

        std::string ptr;
        if (has_global) {
            ptr = module_globals.at(node.name).ptr_value;
        } else {
            ptr = next_register(node.name + "_addr");
            emit_line(ptr + " = alloca i8*");
        }
        emit_line("store i8* " + header_raw + ", i8** " + ptr);
        declare_variable(node.name, VariableInfo{
            "i8*",
            ptr,
            false,
            "",
            0,
            true,
            elem_llvm_type,
            "",
            false,
            "",
            "",
            "",
            "",
            element_type,
        });

        if (node.value) {
            auto& literal = get_node<ArrayLiteral>(node.value);
            const std::string typed_data = next_register(node.name + "_arr_typed_data");
            emit_line(typed_data + " = bitcast i8* " + data_raw + " to " + elem_llvm_type + "*");

            for (std::size_t i = 0; i < literal.elements.size(); ++i) {
                IRValue element_value = cast_value(generate_expression(literal.elements[i]), elem_llvm_type);
                const std::string elem_ptr = next_register(node.name + "_arr_elem_ptr");
                emit_line(elem_ptr + " = getelementptr inbounds " + elem_llvm_type + ", " +
                          elem_llvm_type + "* " + typed_data + ", i64 " + std::to_string(i));
                emit_line("store " + elem_llvm_type + " " + element_value.value + ", " +
                          elem_llvm_type + "* " + elem_ptr);
            }
        }

        return;
    }

    if (node.type == "dict") {
        std::string key_mt_type = node.key_type;
        std::string value_mt_type = node.value_type;

        if (node.value && !is_node<DictLiteral>(node.value)) {
            IRValue existing = cast_value(generate_expression(node.value), "i8*");
            if (key_mt_type.empty()) {
                key_mt_type = "any";
            }
            if (value_mt_type.empty()) {
                value_mt_type = "any";
            }

            std::string ptr;
            if (has_global) {
                ptr = module_globals.at(node.name).ptr_value;
            } else {
                ptr = next_register(node.name + "_addr");
                emit_line(ptr + " = alloca i8*");
            }
            emit_line("store i8* " + existing.value + ", i8** " + ptr);
            declare_variable(node.name, VariableInfo{
                "i8*",
                ptr,
                false,
                "",
                0,
                false,
                "",
                "",
                true,
                map_type_to_llvm(key_mt_type),
                map_type_to_llvm(value_mt_type),
                key_mt_type,
                value_mt_type,
            });
            return;
        }

        std::size_t initial_len = 0;
        if (node.value && is_node<DictLiteral>(node.value)) {
            auto& literal = get_node<DictLiteral>(node.value);
            initial_len = std::min(literal.keys.size(), literal.values.size());
            if (key_mt_type.empty() && !literal.keys.empty()) {
                key_mt_type = infer_literal_mt_type(literal.keys[0]);
            }
            if (value_mt_type.empty() && !literal.values.empty()) {
                value_mt_type = infer_literal_mt_type(literal.values[0]);
            }
        }

        if (key_mt_type.empty()) {
            key_mt_type = "any";
        }
        if (value_mt_type.empty()) {
            value_mt_type = "any";
        }

        std::string key_llvm_type = map_type_to_llvm(key_mt_type);
        std::string value_llvm_type = map_type_to_llvm(value_mt_type);
        if (key_llvm_type == "void") {
            key_llvm_type = "i8*";
            key_mt_type = "any";
        }
        if (value_llvm_type == "void") {
            value_llvm_type = "i8*";
            value_mt_type = "any";
        }

        const std::size_t key_size = llvm_type_size(key_llvm_type);
        const std::size_t value_size = llvm_type_size(value_llvm_type);
        const std::size_t capacity = std::max<std::size_t>(4, initial_len);
        const std::size_t key_bytes = std::max<std::size_t>(1, capacity * key_size);
        const std::size_t value_bytes = std::max<std::size_t>(1, capacity * value_size);

        const std::string header_raw = next_register(node.name + "_dict_hdr_raw");
        emit_line(header_raw + " = call i8* @malloc(i64 32)");

        const std::string len_slot = next_register(node.name + "_dict_len_slot");
        emit_line(len_slot + " = bitcast i8* " + header_raw + " to i64*");
        emit_line("store i64 " + std::to_string(initial_len) + ", i64* " + len_slot);

        const std::string cap_slot = next_register(node.name + "_dict_cap_slot");
        emit_line(cap_slot + " = getelementptr inbounds i64, i64* " + len_slot + ", i64 1");
        emit_line("store i64 " + std::to_string(capacity) + ", i64* " + cap_slot);

        const std::string keys_slot_raw = next_register(node.name + "_dict_keys_slot_raw");
        emit_line(keys_slot_raw + " = getelementptr inbounds i8, i8* " + header_raw + ", i64 16");
        const std::string keys_slot = next_register(node.name + "_dict_keys_slot");
        emit_line(keys_slot + " = bitcast i8* " + keys_slot_raw + " to i8**");

        const std::string values_slot_raw = next_register(node.name + "_dict_values_slot_raw");
        emit_line(values_slot_raw + " = getelementptr inbounds i8, i8* " + header_raw + ", i64 24");
        const std::string values_slot = next_register(node.name + "_dict_values_slot");
        emit_line(values_slot + " = bitcast i8* " + values_slot_raw + " to i8**");

        const std::string keys_raw = next_register(node.name + "_dict_keys_raw");
        emit_line(keys_raw + " = call i8* @malloc(i64 " + std::to_string(key_bytes) + ")");
        emit_line("store i8* " + keys_raw + ", i8** " + keys_slot);

        const std::string values_raw = next_register(node.name + "_dict_values_raw");
        emit_line(values_raw + " = call i8* @malloc(i64 " + std::to_string(value_bytes) + ")");
        emit_line("store i8* " + values_raw + ", i8** " + values_slot);

        std::string ptr;
        if (has_global) {
            ptr = module_globals.at(node.name).ptr_value;
        } else {
            ptr = next_register(node.name + "_addr");
            emit_line(ptr + " = alloca i8*");
        }
        emit_line("store i8* " + header_raw + ", i8** " + ptr);

        declare_variable(node.name, VariableInfo{
            "i8*",
            ptr,
            false,
            "",
            0,
            false,
            "",
            "",
            true,
            key_llvm_type,
            value_llvm_type,
            key_mt_type,
            value_mt_type,
        });

        if (node.value && is_node<DictLiteral>(node.value)) {
            auto& literal = get_node<DictLiteral>(node.value);
            const std::string keys_typed = next_register(node.name + "_dict_keys_typed");
            emit_line(keys_typed + " = bitcast i8* " + keys_raw + " to " + key_llvm_type + "*");
            const std::string values_typed = next_register(node.name + "_dict_values_typed");
            emit_line(values_typed + " = bitcast i8* " + values_raw + " to " + value_llvm_type + "*");

            const std::size_t pair_count = std::min(literal.keys.size(), literal.values.size());
            for (std::size_t i = 0; i < pair_count; ++i) {
                IRValue key_value = cast_value(generate_expression(literal.keys[i]), key_llvm_type);
                IRValue value_value = cast_value(generate_expression(literal.values[i]), value_llvm_type);

                const std::string key_ptr = next_register(node.name + "_dict_key_ptr");
                emit_line(key_ptr + " = getelementptr inbounds " + key_llvm_type + ", " +
                          key_llvm_type + "* " + keys_typed + ", i64 " + std::to_string(i));
                emit_line("store " + key_llvm_type + " " + key_value.value + ", " +
                          key_llvm_type + "* " + key_ptr);

                const std::string value_ptr = next_register(node.name + "_dict_value_ptr");
                emit_line(value_ptr + " = getelementptr inbounds " + value_llvm_type + ", " +
                          value_llvm_type + "* " + values_typed + ", i64 " + std::to_string(i));
                emit_line("store " + value_llvm_type + " " + value_value.value + ", " +
                          value_llvm_type + "* " + value_ptr);
            }
        }

        return;
    }

    const std::string llvm_type = map_type_to_llvm(node.type);
    if (llvm_type == "void") {
        throw std::runtime_error("Cannot declare variable of type void");
    }

    const std::string ptr = next_register(node.name + "_addr");
    emit_line(ptr + " = alloca " + llvm_type);
    const std::string class_name = (!is_builtin_mt_type(node.type) && classes.find(node.type) != classes.end())
        ? node.type : "";
    declare_variable(node.name, VariableInfo{
        llvm_type,
        ptr,
        false,
        "",
        0,
        false,
        "",
        class_name,
        false,
        "",
        "",
        "",
        "",
    });

    IRValue init;
    if (node.value) {
        init = generate_expression(node.value);
        init = cast_value(init, llvm_type);
    } else {
        if (llvm_type == "i1") {
            init = IRValue{"i1", "0", true};
        } else if (llvm_type == "double") {
            init = IRValue{"double", "0.0", true};
        } else if (is_pointer_type(llvm_type)) {
            init = IRValue{llvm_type, "null", true};
        } else {
            init = IRValue{llvm_type, "0", true};
        }
    }

    emit_line("store " + llvm_type + " " + init.value + ", " + llvm_type + "* " + ptr);
}

void CodeGenerator::generate_set_statement(SetStatement& node) {
    if (node.target && is_node<Identifier>(node.target)) {
        const std::string name = get_node<Identifier>(node.target).name;
        const VariableInfo* var = resolve_variable(name);
        if (!var) {
            throw std::runtime_error("Assignment to undeclared variable '" + name + "'");
        }
        if (var->is_fixed_array) {
            throw std::runtime_error(
                "Cannot assign to fixed-size array '" + name +
                "' directly; assign through an index (set " + name + "[i] = ...)");
        }

        IRValue value = generate_expression(node.value);
        value = cast_value(value, var->llvm_type);
        emit_line("store " + var->llvm_type + " " + value.value + ", " +
                  var->llvm_type + "* " + var->ptr_value);
        return;
    }

    if (node.target && is_node<IndexExpression>(node.target)) {
        auto& index_expr = get_node<IndexExpression>(node.target);
        IRValue raw_index = generate_expression(index_expr.index);
        const CodegenClassFieldInfo* object_member_field = nullptr;
        if (index_expr.object && is_node<MemberExpression>(index_expr.object)) {
            const auto& member = get_node<MemberExpression>(index_expr.object);
            const VariableInfo* owner_var = nullptr;
            if (member.object && is_node<Identifier>(member.object)) {
                owner_var = resolve_variable(get_node<Identifier>(member.object).name);
            } else if (member.object && is_node<ThisExpression>(member.object)) {
                owner_var = resolve_variable("this");
            }
            if (owner_var && !owner_var->class_name.empty()) {
                const auto class_it = classes.find(owner_var->class_name);
                if (class_it != classes.end()) {
                    const auto field_it = class_it->second.fields.find(member.property);
                    if (field_it != class_it->second.fields.end()) {
                        object_member_field = &field_it->second;
                    }
                }
            }
        }

        if (object_member_field && object_member_field->mt_type == "array") {
            IRValue index = cast_value(raw_index, "i64");
            IRValue header = cast_value(generate_expression(index_expr.object), "i8*");

            std::string elem_llvm_type = "i8*";
            if (!object_member_field->element_mt_type.empty()) {
                elem_llvm_type = map_type_to_llvm(object_member_field->element_mt_type);
                if (elem_llvm_type == "void") {
                    elem_llvm_type = "i8*";
                }
            }

            const std::string data_slot_raw = next_register("member_arr_data_slot_raw");
            emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header.value + ", i64 16");
            const std::string data_slot = next_register("member_arr_data_slot");
            emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");
            const std::string data_raw = next_register("member_arr_data_raw");
            emit_line(data_raw + " = load i8*, i8** " + data_slot);

            const std::string typed_data = next_register("member_arr_typed_data");
            emit_line(typed_data + " = bitcast i8* " + data_raw + " to " + elem_llvm_type + "*");
            const std::string elem_ptr = next_register("member_arr_elem_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + elem_llvm_type +
                      ", " + elem_llvm_type + "* " + typed_data + ", i64 " + index.value);

            IRValue value = cast_value(generate_expression(node.value), elem_llvm_type);
            emit_line("store " + elem_llvm_type + " " + value.value + ", " +
                      elem_llvm_type + "* " + elem_ptr);
            return;
        }

        if (!index_expr.object || !is_node<Identifier>(index_expr.object)) {
            throw std::runtime_error(
                "Indexed assignment is only supported on array identifiers or class array fields");
        }

        const std::string object_name = get_node<Identifier>(index_expr.object).name;
        const VariableInfo* var = resolve_variable(object_name);
        if (!var) {
            throw std::runtime_error("Assignment to undeclared variable '" + object_name + "'");
        }

        if (var->is_fixed_array) {
            IRValue index = cast_value(raw_index, "i64");
            const std::string array_type = "[" + std::to_string(var->fixed_array_size) + " x " +
                                           var->fixed_array_elem_llvm_type + "]";
            const std::string elem_ptr = next_register(object_name + "_elem_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + array_type + ", " +
                      array_type + "* " + var->ptr_value + ", i64 0, i64 " + index.value);

            IRValue value = cast_value(generate_expression(node.value), var->fixed_array_elem_llvm_type);
            emit_line("store " + var->fixed_array_elem_llvm_type + " " + value.value + ", " +
                      var->fixed_array_elem_llvm_type + "* " + elem_ptr);
            return;
        }

        if (var->is_dynamic_array) {
            IRValue index = cast_value(raw_index, "i64");
            const std::string header = next_register(object_name + "_hdr");
            emit_line(header + " = load i8*, i8** " + var->ptr_value);

            const std::string data_slot_raw = next_register(object_name + "_data_slot_raw");
            emit_line(data_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 16");
            const std::string data_slot = next_register(object_name + "_data_slot");
            emit_line(data_slot + " = bitcast i8* " + data_slot_raw + " to i8**");
            const std::string data_raw = next_register(object_name + "_data_raw");
            emit_line(data_raw + " = load i8*, i8** " + data_slot);

            const std::string typed_data = next_register(object_name + "_typed_data");
            emit_line(typed_data + " = bitcast i8* " + data_raw + " to " +
                      var->dynamic_array_elem_llvm_type + "*");
            const std::string elem_ptr = next_register(object_name + "_elem_ptr");
            emit_line(elem_ptr + " = getelementptr inbounds " + var->dynamic_array_elem_llvm_type +
                      ", " + var->dynamic_array_elem_llvm_type + "* " + typed_data + ", i64 " +
                      index.value);

            IRValue value = cast_value(generate_expression(node.value), var->dynamic_array_elem_llvm_type);
            emit_line("store " + var->dynamic_array_elem_llvm_type + " " + value.value + ", " +
                      var->dynamic_array_elem_llvm_type + "* " + elem_ptr);
            return;
        }

        if (var->is_dict) {
            const std::string key_type = var->dict_key_llvm_type.empty() ? "i8*" : var->dict_key_llvm_type;
            const std::string value_type = var->dict_value_llvm_type.empty() ? "i8*" : var->dict_value_llvm_type;
            IRValue key = cast_value(raw_index, key_type);
            IRValue value = cast_value(generate_expression(node.value), value_type);

            const std::string header = next_register(object_name + "_dict_hdr");
            emit_line(header + " = load i8*, i8** " + var->ptr_value);

            const std::string len_slot = next_register(object_name + "_dict_len_slot");
            emit_line(len_slot + " = bitcast i8* " + header + " to i64*");
            const std::string len = next_register(object_name + "_dict_len");
            emit_line(len + " = load i64, i64* " + len_slot);

            const std::string cap_slot = next_register(object_name + "_dict_cap_slot");
            emit_line(cap_slot + " = getelementptr inbounds i64, i64* " + len_slot + ", i64 1");
            const std::string cap = next_register(object_name + "_dict_cap");
            emit_line(cap + " = load i64, i64* " + cap_slot);

            const std::string keys_slot_raw = next_register(object_name + "_dict_keys_slot_raw");
            emit_line(keys_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 16");
            const std::string keys_slot = next_register(object_name + "_dict_keys_slot");
            emit_line(keys_slot + " = bitcast i8* " + keys_slot_raw + " to i8**");
            const std::string keys_raw = next_register(object_name + "_dict_keys_raw");
            emit_line(keys_raw + " = load i8*, i8** " + keys_slot);
            const std::string keys_typed = next_register(object_name + "_dict_keys_typed");
            emit_line(keys_typed + " = bitcast i8* " + keys_raw + " to " + key_type + "*");

            const std::string values_slot_raw = next_register(object_name + "_dict_values_slot_raw");
            emit_line(values_slot_raw + " = getelementptr inbounds i8, i8* " + header + ", i64 24");
            const std::string values_slot = next_register(object_name + "_dict_values_slot");
            emit_line(values_slot + " = bitcast i8* " + values_slot_raw + " to i8**");
            const std::string values_raw = next_register(object_name + "_dict_values_raw");
            emit_line(values_raw + " = load i8*, i8** " + values_slot);
            const std::string values_typed = next_register(object_name + "_dict_values_typed");
            emit_line(values_typed + " = bitcast i8* " + values_raw + " to " + value_type + "*");

            const std::string idx_ptr = next_register(object_name + "_dict_idx_ptr");
            emit_line(idx_ptr + " = alloca i64");
            emit_line("store i64 0, i64* " + idx_ptr);
            const std::string found_ptr = next_register(object_name + "_dict_found_ptr");
            emit_line(found_ptr + " = alloca i1");
            emit_line("store i1 0, i1* " + found_ptr);
            const std::string found_idx_ptr = next_register(object_name + "_dict_found_idx_ptr");
            emit_line(found_idx_ptr + " = alloca i64");
            emit_line("store i64 0, i64* " + found_idx_ptr);

            const std::string cond_label = next_label("dict_set_cond");
            const std::string body_label = next_label("dict_set_body");
            const std::string hit_label = next_label("dict_set_hit");
            const std::string next_label_name = next_label("dict_set_next");
            const std::string after_search_label = next_label("dict_set_after_search");
            const std::string update_label = next_label("dict_set_update");
            const std::string insert_label = next_label("dict_set_insert");
            const std::string grow_check_label = next_label("dict_set_grow_check");
            const std::string grow_label = next_label("dict_set_grow");
            const std::string append_label = next_label("dict_set_append");
            const std::string end_label = next_label("dict_set_end");

            emit_line("br label %" + cond_label);
            emit_label(cond_label);
            const std::string idx = next_register(object_name + "_dict_idx");
            emit_line(idx + " = load i64, i64* " + idx_ptr);
            const std::string cond = next_register(object_name + "_dict_cond");
            emit_line(cond + " = icmp slt i64 " + idx + ", " + len);
            emit_line("br i1 " + cond + ", label %" + body_label + ", label %" + after_search_label);

            emit_label(body_label);
            const std::string key_ptr = next_register(object_name + "_dict_key_ptr");
            emit_line(key_ptr + " = getelementptr inbounds " + key_type + ", " + key_type + "* " +
                      keys_typed + ", i64 " + idx);
            const std::string key_loaded = next_register(object_name + "_dict_key_loaded");
            emit_line(key_loaded + " = load " + key_type + ", " + key_type + "* " + key_ptr);

            std::string key_match;
            if (key_type == "i8*") {
                const std::string cmp = next_register("dict_set_key_cmp");
                emit_line(cmp + " = call i32 @strcmp(i8* " + key_loaded + ", i8* " + key.value + ")");
                key_match = next_register("dict_set_key_match");
                emit_line(key_match + " = icmp eq i32 " + cmp + ", 0");
            } else if (key_type == "double") {
                key_match = next_register("dict_set_key_match");
                emit_line(key_match + " = fcmp oeq double " + key_loaded + ", " + key.value);
            } else {
                key_match = next_register("dict_set_key_match");
                emit_line(key_match + " = icmp eq " + key_type + " " + key_loaded + ", " + key.value);
            }

            emit_line("br i1 " + key_match + ", label %" + hit_label + ", label %" + next_label_name);

            emit_label(hit_label);
            emit_line("store i1 1, i1* " + found_ptr);
            emit_line("store i64 " + idx + ", i64* " + found_idx_ptr);
            emit_line("br label %" + after_search_label);

            emit_label(next_label_name);
            const std::string next_idx = next_register(object_name + "_dict_next_idx");
            emit_line(next_idx + " = add i64 " + idx + ", 1");
            emit_line("store i64 " + next_idx + ", i64* " + idx_ptr);
            emit_line("br label %" + cond_label);

            emit_label(after_search_label);
            const std::string found = next_register(object_name + "_dict_found");
            emit_line(found + " = load i1, i1* " + found_ptr);
            emit_line("br i1 " + found + ", label %" + update_label + ", label %" + insert_label);

            emit_label(update_label);
            const std::string found_idx = next_register(object_name + "_dict_found_idx");
            emit_line(found_idx + " = load i64, i64* " + found_idx_ptr);
            const std::string update_value_ptr = next_register(object_name + "_dict_update_value_ptr");
            emit_line(update_value_ptr + " = getelementptr inbounds " + value_type + ", " + value_type +
                      "* " + values_typed + ", i64 " + found_idx);
            emit_line("store " + value_type + " " + value.value + ", " + value_type + "* " + update_value_ptr);
            emit_line("br label %" + end_label);

            emit_label(insert_label);
            emit_line("br label %" + grow_check_label);

            emit_label(grow_check_label);
            const std::string needs_grow = next_register(object_name + "_dict_needs_grow");
            emit_line(needs_grow + " = icmp eq i64 " + len + ", " + cap);
            emit_line("br i1 " + needs_grow + ", label %" + grow_label + ", label %" + append_label);

            emit_label(grow_label);
            const std::string cap_is_zero = next_register(object_name + "_dict_cap_zero");
            emit_line(cap_is_zero + " = icmp eq i64 " + cap + ", 0");
            const std::string doubled_cap = next_register(object_name + "_dict_doubled_cap");
            emit_line(doubled_cap + " = mul i64 " + cap + ", 2");
            const std::string new_cap = next_register(object_name + "_dict_new_cap");
            emit_line(new_cap + " = select i1 " + cap_is_zero + ", i64 4, i64 " + doubled_cap);

            const std::string key_bytes = next_register(object_name + "_dict_key_bytes");
            emit_line(key_bytes + " = mul i64 " + new_cap + ", " + std::to_string(llvm_type_size(key_type)));
            const std::string value_bytes = next_register(object_name + "_dict_value_bytes");
            emit_line(value_bytes + " = mul i64 " + new_cap + ", " + std::to_string(llvm_type_size(value_type)));

            const std::string grown_keys = next_register(object_name + "_dict_grown_keys");
            emit_line(grown_keys + " = call i8* @realloc(i8* " + keys_raw + ", i64 " + key_bytes + ")");
            emit_line("store i8* " + grown_keys + ", i8** " + keys_slot);

            const std::string grown_values = next_register(object_name + "_dict_grown_values");
            emit_line(grown_values + " = call i8* @realloc(i8* " + values_raw + ", i64 " + value_bytes + ")");
            emit_line("store i8* " + grown_values + ", i8** " + values_slot);

            emit_line("store i64 " + new_cap + ", i64* " + cap_slot);
            emit_line("br label %" + append_label);

            emit_label(append_label);
            const std::string latest_len = next_register(object_name + "_dict_latest_len");
            emit_line(latest_len + " = load i64, i64* " + len_slot);
            const std::string latest_keys_raw = next_register(object_name + "_dict_latest_keys_raw");
            emit_line(latest_keys_raw + " = load i8*, i8** " + keys_slot);
            const std::string latest_values_raw = next_register(object_name + "_dict_latest_values_raw");
            emit_line(latest_values_raw + " = load i8*, i8** " + values_slot);
            const std::string latest_keys_typed = next_register(object_name + "_dict_latest_keys_typed");
            emit_line(latest_keys_typed + " = bitcast i8* " + latest_keys_raw + " to " + key_type + "*");
            const std::string latest_values_typed = next_register(object_name + "_dict_latest_values_typed");
            emit_line(latest_values_typed + " = bitcast i8* " + latest_values_raw + " to " + value_type + "*");

            const std::string append_key_ptr = next_register(object_name + "_dict_append_key_ptr");
            emit_line(append_key_ptr + " = getelementptr inbounds " + key_type + ", " + key_type + "* " +
                      latest_keys_typed + ", i64 " + latest_len);
            emit_line("store " + key_type + " " + key.value + ", " + key_type + "* " + append_key_ptr);

            const std::string append_value_ptr = next_register(object_name + "_dict_append_value_ptr");
            emit_line(append_value_ptr + " = getelementptr inbounds " + value_type + ", " + value_type + "* " +
                      latest_values_typed + ", i64 " + latest_len);
            emit_line("store " + value_type + " " + value.value + ", " + value_type + "* " + append_value_ptr);

            const std::string new_len = next_register(object_name + "_dict_new_len");
            emit_line(new_len + " = add i64 " + latest_len + ", 1");
            emit_line("store i64 " + new_len + ", i64* " + len_slot);
            emit_line("br label %" + end_label);

            emit_label(end_label);
            return;
        }

        throw std::runtime_error("Indexed assignment is only supported for fixed-size or dynamic arrays");
    }

    if (node.target && is_node<MemberExpression>(node.target)) {
        auto& member = get_node<MemberExpression>(node.target);
        const VariableInfo* obj_var = nullptr;
        std::string object_name;
        if (member.object && is_node<Identifier>(member.object)) {
            object_name = get_node<Identifier>(member.object).name;
            obj_var = resolve_variable(object_name);
        } else if (member.object && is_node<ThisExpression>(member.object)) {
            object_name = "this";
            obj_var = resolve_variable("this");
        }
        if (!obj_var || obj_var->class_name.empty()) {
            throw std::runtime_error("Field assignment requires class object identifier or this");
        }

        const auto class_it = classes.find(obj_var->class_name);
        if (class_it == classes.end()) {
            throw std::runtime_error("Unknown class type '" + obj_var->class_name + "'");
        }
        const auto field_it = class_it->second.fields.find(member.property);
        if (field_it == class_it->second.fields.end()) {
            throw std::runtime_error("Unknown field '" + member.property + "' for class '" +
                                     obj_var->class_name + "'");
        }

        const std::string obj_ptr = next_register(object_name + "_obj");
        emit_line(obj_ptr + " = load i8*, i8** " + obj_var->ptr_value);
        const std::string field_raw = next_register(object_name + "_field_raw");
        emit_line(field_raw + " = getelementptr inbounds i8, i8* " + obj_ptr + ", i64 " +
                  std::to_string(field_it->second.offset));
        const std::string field_ptr = next_register(object_name + "_field_ptr");
        emit_line(field_ptr + " = bitcast i8* " + field_raw + " to " + field_it->second.llvm_type + "*");
        IRValue value = cast_value(generate_expression(node.value), field_it->second.llvm_type);
        emit_line("store " + field_it->second.llvm_type + " " + value.value + ", " +
                  field_it->second.llvm_type + "* " + field_ptr);
        return;
    }

    throw std::runtime_error("Unsupported assignment target in codegen");
}

void CodeGenerator::generate_expression_statement(ExpressionStatement& node) {
    (void)generate_expression(node.expression);
}

void CodeGenerator::emit_restore_try_contexts_for_return() {
    if (try_contexts.empty()) {
        return;
    }

    const std::string restore_prev = next_register("ret_restore_prev_jmp");
    emit_line(restore_prev + " = load i8*, i8** " + try_contexts.front().saved_prev_jmp_addr);
    emit_line("store i8* " + restore_prev + ", i8** @__mt_exc_jmp");
}

void CodeGenerator::generate_return_statement(ReturnStatement& node) {
    emit_restore_try_contexts_for_return();

    if (current_return_type == "void") {
        emit_line("ret void");
        return;
    }

    if (!node.value) {
        if (current_return_type == "i1") {
            emit_line("ret i1 0");
        } else if (current_return_type == "double") {
            emit_line("ret double 0.0");
        } else if (is_pointer_type(current_return_type)) {
            emit_line("ret " + current_return_type + " null");
        } else {
            emit_line("ret " + current_return_type + " 0");
        }
        return;
    }

    IRValue value = cast_value(generate_expression(node.value), current_return_type);
    emit_line("ret " + current_return_type + " " + value.value);
}

void CodeGenerator::generate_block(Block& node) {
    push_scope();
    for (auto& statement : node.statements) {
        if (current_block_terminated) {
            break;
        }
        generate_statement(statement);
    }
    pop_scope();
}

void CodeGenerator::generate_if_statement(IfStatement& node) {
    IRValue cond = ensure_boolean(generate_expression(node.condition));

    const std::string then_label = next_label("if_then");
    const std::string else_label = next_label("if_else");
    const std::string end_label = next_label("if_end");

    emit_line("br i1 " + cond.value + ", label %" + then_label + ", label %" + else_label);

    emit_label(then_label);
    if (node.then_body && is_node<Block>(node.then_body)) {
        generate_block(get_node<Block>(node.then_body));
    }
    const bool then_terminated = current_block_terminated;
    if (!then_terminated) {
        emit_line("br label %" + end_label);
    }

    emit_label(else_label);
    if (node.else_body && is_node<Block>(node.else_body)) {
        generate_block(get_node<Block>(node.else_body));
    }
    const bool else_terminated = current_block_terminated;
    if (!else_terminated) {
        emit_line("br label %" + end_label);
    }

    emit_label(end_label);
}

void CodeGenerator::generate_while_statement(WhileStatement& node) {
    const std::string cond_label = next_label("while_cond");
    const std::string body_label = next_label("while_body");
    const std::string end_label = next_label("while_end");

    emit_line("br label %" + cond_label);

    emit_label(cond_label);
    IRValue cond = ensure_boolean(generate_expression(node.condition));
    emit_line("br i1 " + cond.value + ", label %" + body_label + ", label %" + end_label);

    emit_label(body_label);
    break_labels.push_back(end_label);
    if (node.then_body && is_node<Block>(node.then_body)) {
        generate_block(get_node<Block>(node.then_body));
    }
    break_labels.pop_back();

    if (!current_block_terminated) {
        emit_line("br label %" + cond_label);
    }

    emit_label(end_label);
}

void CodeGenerator::generate_break_statement(BreakStatement&) {
    if (break_labels.empty()) {
        throw std::runtime_error("break used outside of loop in codegen");
    }
    emit_line("br label %" + break_labels.back());
}

std::string CodeGenerator::infer_class_name_from_ast(ASTNode& node) {
    if (!node) {
        return "";
    }

    if (is_node<NewExpression>(node)) {
        return get_node<NewExpression>(node).class_name;
    }

    if (is_node<Identifier>(node)) {
        const std::string name = get_node<Identifier>(node).name;
        const VariableInfo* var = lookup_variable(name);
        if (var && !var->class_name.empty()) {
            return var->class_name;
        }
        return "";
    }

    if (is_node<ThisExpression>(node)) {
        const VariableInfo* var = lookup_variable("this");
        if (var && !var->class_name.empty()) {
            return var->class_name;
        }
        return "";
    }

    if (is_node<MemberExpression>(node)) {
        auto& member = get_node<MemberExpression>(node);
        const VariableInfo* owner_var = nullptr;
        if (member.object && is_node<Identifier>(member.object)) {
            owner_var = lookup_variable(get_node<Identifier>(member.object).name);
        } else if (member.object && is_node<ThisExpression>(member.object)) {
            owner_var = lookup_variable("this");
        }
        if (!owner_var || owner_var->class_name.empty()) {
            return "";
        }

        const auto class_it = classes.find(owner_var->class_name);
        if (class_it == classes.end()) {
            return "";
        }
        const auto field_it = class_it->second.fields.find(member.property);
        if (field_it == class_it->second.fields.end()) {
            return "";
        }
        if (classes.find(field_it->second.mt_type) != classes.end()) {
            return field_it->second.mt_type;
        }
    }

    if (is_node<CallExpression>(node)) {
        auto& call = get_node<CallExpression>(node);
        if (call.callee && is_node<Identifier>(call.callee)) {
            const std::string callee = get_node<Identifier>(call.callee).name;
            const auto fn_it = functions.find(callee);
            if (fn_it != functions.end() &&
                classes.find(fn_it->second.return_mt_type) != classes.end()) {
                return fn_it->second.return_mt_type;
            }
        } else if (call.callee && is_node<MemberExpression>(call.callee)) {
            auto& member_callee = get_node<MemberExpression>(call.callee);
            std::string owner_class = infer_class_name_from_ast(member_callee.object);
            if (owner_class.empty()) {
                return "";
            }
            const auto class_it = classes.find(owner_class);
            if (class_it == classes.end()) {
                return "";
            }
            const auto method_it = class_it->second.methods.find(member_callee.property);
            if (method_it == class_it->second.methods.end()) {
                return "";
            }
            if (classes.find(method_it->second.return_mt_type) != classes.end()) {
                return method_it->second.return_mt_type;
            }
        }
    }

    return "";
}

bool CodeGenerator::class_is_a(const std::string& derived_class, const std::string& base_class) const {
    if (derived_class.empty() || base_class.empty()) {
        return false;
    }
    if (derived_class == base_class) {
        return true;
    }

    auto current_it = classes.find(derived_class);
    while (current_it != classes.end()) {
        const std::string parent = current_it->second.parent_class;
        if (parent.empty()) {
            return false;
        }
        if (parent == base_class) {
            return true;
        }
        current_it = classes.find(parent);
    }

    return false;
}

int CodeGenerator::class_tag_for_name(const std::string& class_name) const {
    if (class_name.empty()) {
        return 0;
    }
    const auto it = class_type_tags.find(class_name);
    if (it == class_type_tags.end()) {
        return 0;
    }
    return it->second;
}

CodeGenerator::IRValue CodeGenerator::cast_value(const IRValue& value, const std::string& target_type) {
    if (!value.is_valid) {
        return value;
    }
    if (value.type == target_type) {
        return value;
    }

    if (target_type == "i1") {
        return ensure_boolean(value);
    }

    if (value.type == "i1" && target_type == "i32") {
        const std::string reg = next_register("zext");
        emit_line(reg + " = zext i1 " + value.value + " to i32");
        return IRValue{"i32", reg, true};
    }

    if (value.type == "i1" && target_type == "i64") {
        const std::string reg = next_register("zext");
        emit_line(reg + " = zext i1 " + value.value + " to i64");
        return IRValue{"i64", reg, true};
    }

    if (value.type == "i1" && target_type == "double") {
        const std::string reg = next_register("bool_to_double");
        emit_line(reg + " = uitofp i1 " + value.value + " to double");
        return IRValue{"double", reg, true};
    }

    if (value.type == "i32" && target_type == "double") {
        const std::string reg = next_register("sitofp");
        emit_line(reg + " = sitofp i32 " + value.value + " to double");
        return IRValue{"double", reg, true};
    }

    if (value.type == "i32" && target_type == "i64") {
        const std::string reg = next_register("sext");
        emit_line(reg + " = sext i32 " + value.value + " to i64");
        return IRValue{"i64", reg, true};
    }

    if (value.type == "i64" && target_type == "i32") {
        const std::string reg = next_register("trunc");
        emit_line(reg + " = trunc i64 " + value.value + " to i32");
        return IRValue{"i32", reg, true};
    }

    if (value.type == "double" && target_type == "i32") {
        const std::string reg = next_register("fptosi");
        emit_line(reg + " = fptosi double " + value.value + " to i32");
        return IRValue{"i32", reg, true};
    }

    if (value.type == "i8*" && target_type == "i32") {
        const std::string reg = next_register("ptrtoint");
        emit_line(reg + " = ptrtoint i8* " + value.value + " to i32");
        return IRValue{"i32", reg, true};
    }

    if (value.type == "i32" && target_type == "i8*") {
        const std::string reg = next_register("inttoptr");
        emit_line(reg + " = inttoptr i32 " + value.value + " to i8*");
        return IRValue{"i8*", reg, true};
    }

    if (value.type == "i64" && target_type == "i8*") {
        const std::string reg = next_register("inttoptr");
        emit_line(reg + " = inttoptr i64 " + value.value + " to i8*");
        return IRValue{"i8*", reg, true};
    }

    if (value.type == "i1" && target_type == "i8*") {
        const std::string wide = next_register("zext");
        emit_line(wide + " = zext i1 " + value.value + " to i64");
        const std::string reg = next_register("inttoptr");
        emit_line(reg + " = inttoptr i64 " + wide + " to i8*");
        return IRValue{"i8*", reg, true};
    }

    if (value.type == "double" && target_type == "i8*") {
        const std::string bits = next_register("double_bits");
        emit_line(bits + " = bitcast double " + value.value + " to i64");
        const std::string reg = next_register("inttoptr");
        emit_line(reg + " = inttoptr i64 " + bits + " to i8*");
        return IRValue{"i8*", reg, true};
    }

    if (value.type == "i8*" && target_type == "i64") {
        const std::string reg = next_register("ptrtoint");
        emit_line(reg + " = ptrtoint i8* " + value.value + " to i64");
        return IRValue{"i64", reg, true};
    }

    if (value.type == "i8*" && target_type == "double") {
        const std::string bits = next_register("ptrtoint");
        emit_line(bits + " = ptrtoint i8* " + value.value + " to i64");
        const std::string reg = next_register("bits_to_double");
        emit_line(reg + " = bitcast i64 " + bits + " to double");
        return IRValue{"double", reg, true};
    }

    if (value.type == "i8*" && target_type == "i1") {
        const std::string bits = next_register("ptrtoint");
        emit_line(bits + " = ptrtoint i8* " + value.value + " to i64");
        const std::string reg = next_register("tobool");
        emit_line(reg + " = icmp ne i64 " + bits + ", 0");
        return IRValue{"i1", reg, true};
    }

    if (is_pointer_type(value.type) && is_pointer_type(target_type)) {
        const std::string reg = next_register("bitcast");
        emit_line(reg + " = bitcast " + value.type + " " + value.value + " to " + target_type);
        return IRValue{target_type, reg, true};
    }

    if (value.value == "null" && is_pointer_type(target_type)) {
        return IRValue{target_type, "null", true};
    }

    throw std::runtime_error("Unsupported cast from " + value.type + " to " + target_type);
}

CodeGenerator::IRValue CodeGenerator::ensure_boolean(const IRValue& value) {
    if (value.type == "i1") {
        return value;
    }

    if (value.type == "i32") {
        const std::string reg = next_register("tobool");
        emit_line(reg + " = icmp ne i32 " + value.value + ", 0");
        return IRValue{"i1", reg, true};
    }

    if (value.type == "double") {
        const std::string reg = next_register("tobool");
        emit_line(reg + " = fcmp one double " + value.value + ", 0.0");
        return IRValue{"i1", reg, true};
    }

    if (is_pointer_type(value.type)) {
        const std::string reg = next_register("tobool");
        emit_line(reg + " = icmp ne " + value.type + " " + value.value + ", null");
        return IRValue{"i1", reg, true};
    }

    throw std::runtime_error("Cannot convert type " + value.type + " to bool");
}

CodeGenerator::StringConstantInfo CodeGenerator::get_or_create_string_constant(const std::string& value) {
    const auto found = string_constants.find(value);
    if (found != string_constants.end()) {
        return found->second;
    }

    const StringConstantInfo info{ "@.str." + std::to_string(string_counter++), value.size() + 1 };
    const std::string escaped = escape_llvm_string(value) + "\\00";

    global_lines.push_back(info.symbol + " = private unnamed_addr constant [" +
                           std::to_string(info.length) + " x i8] c\"" + escaped + "\", align 1");

    string_constants[value] = info;
    return info;
}

std::string CodeGenerator::string_constant_gep(const StringConstantInfo& info) const {
    return "getelementptr inbounds ([" + std::to_string(info.length) + " x i8], [" +
           std::to_string(info.length) + " x i8]* " + info.symbol + ", i64 0, i64 0)";
}

std::string CodeGenerator::escape_llvm_string(const std::string& value) const {
    std::ostringstream out;
    for (unsigned char ch : value) {
        if (ch == '\\') {
            out << "\\5C";
        } else if (ch == '"') {
            out << "\\22";
        } else if (std::isprint(ch) && ch != '\n' && ch != '\r' && ch != '\t') {
            out << static_cast<char>(ch);
        } else {
            out << "\\" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(ch) << std::nouppercase << std::dec;
        }
    }
    return out.str();
}

std::string CodeGenerator::infer_type_name(const IRValue& value) const {
    if (value.type == "i32") {
        return "int";
    }
    if (value.type == "double") {
        return "float";
    }
    if (value.type == "i1") {
        return "bool";
    }
    if (value.type == "i8*") {
        return "string";
    }
    return "unknown";
}

std::size_t CodeGenerator::llvm_type_size(const std::string& llvm_type) const {
    if (llvm_type == "i1") {
        return 1;
    }
    if (llvm_type == "i32") {
        return 4;
    }
    if (llvm_type == "double") {
        return 8;
    }
    if (is_pointer_type(llvm_type)) {
        return 8;
    }
    return 8;
}

std::size_t CodeGenerator::align_up(std::size_t value, std::size_t alignment) const {
    if (alignment <= 1) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + (alignment - remainder);
}
