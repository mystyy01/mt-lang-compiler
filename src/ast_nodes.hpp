#pragma once

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <optional>
#include <type_traits>
#include <utility>
#include "tokenizer.hpp"

// Forward declarations
struct ASTNodeData;
using ASTNode = std::unique_ptr<ASTNodeData>;

// Sub-structures (not in the variant, used inside other nodes)
struct Parameter {
    std::string name;
    std::string param_type;
    ASTNode default_value;
    std::string element_type;

    Parameter() = default;
    Parameter(const std::string& n, const std::string& pt = "",
              ASTNode dv = nullptr, const std::string& et = "")
        : name(n), param_type(pt), default_value(std::move(dv)), element_type(et) {}

    // Move-only due to ASTNode
    Parameter(Parameter&&) = default;
    Parameter& operator=(Parameter&&) = default;
};

struct FieldDeclaration {
    std::string name;
    std::string type;
    ASTNode initializer;
    bool is_constructor_arg = false;
    std::string element_type;
    int line = -1;
    int column = -1;
};

struct MethodDeclaration {
    std::string name;
    std::vector<Parameter> params;
    std::string return_type;
    ASTNode body;  // Block node
    bool is_virtual = false;
    bool is_static = false;
    int line = -1;
    int column = -1;
};

struct CatchBlock {
    std::string exception_type;
    std::string identifier;
    ASTNode body;  // Block node
    int line = -1;
    int column = -1;
};

// AST node types
struct NumberLiteral { int value; int line = -1; int column = -1; };
struct FloatLiteral { double value; int line = -1; int column = -1; };
struct StringLiteral { std::string value; int line = -1; int column = -1; };
struct BoolLiteral { bool value; };
struct NullLiteral {};
struct TypeLiteral { std::string name; };

struct Identifier { std::string name; int line = -1; int column = -1; };
struct ThisExpression { int line = -1; int column = -1; };

struct BinaryExpression { ASTNode left; Token op; ASTNode right; };
struct InExpression { ASTNode item; ASTNode container; int line = -1; int column = -1; };

struct ArrayLiteral { std::vector<ASTNode> elements; };
struct DictLiteral {
    std::vector<ASTNode> keys;
    std::vector<ASTNode> values;
    std::string key_type;
    std::string value_type;
};

struct VariableDeclaration {
    std::string type;
    std::string name;
    ASTNode value;
    std::string element_type;
    int fixed_size = -1;  // For C-style stack arrays: int[16] buf
    bool is_dynamic = false;
    std::string key_type;
    std::string value_type;
    int line = -1;
    int column = -1;
};

struct SetStatement { ASTNode target; ASTNode value; int line = -1; int column = -1; };
struct BreakStatement { int line = -1; int column = -1; };
struct ReturnStatement { ASTNode value; };
struct Block { std::vector<ASTNode> statements; };
struct ExpressionStatement { ASTNode expression; };

struct CallExpression {
    ASTNode callee;
    std::vector<ASTNode> arguments;
    int line = -1;
    int column = -1;
};

struct MemberExpression { ASTNode object; std::string property; };
struct IndexExpression { ASTNode object; ASTNode index; };

struct TypeofExpression { ASTNode argument; };
struct HasattrExpression {
    ASTNode obj;
    std::string attr_name;
    bool compile_time_result = false;  // set by semantic analyzer
};
struct ClassofExpression { ASTNode argument; };

struct IfStatement { ASTNode condition; ASTNode then_body; ASTNode else_body; };
struct WhileStatement { ASTNode condition; ASTNode then_body; };
struct ForInStatement { std::string variable; ASTNode iterable; ASTNode body; };

struct FunctionDeclaration {
    std::string return_type;
    std::string name;
    std::vector<Parameter> parameters;
    ASTNode body;
};

struct ExternalDeclaration {
    std::string return_type;
    std::string name;
    std::vector<Parameter> parameters;
};

struct DynamicFunctionDeclaration {
    std::string name;
    std::vector<Parameter> parameters;
    ASTNode body;
};

struct FromImportStatement {
    ASTNode module_path;
    std::vector<std::string> symbols;
    bool is_wildcard = false;
};

struct SimpleImportStatement {
    std::string module_name;
    std::string alias;
};

struct LibcImportStatement {
    std::vector<std::string> symbols;
};

struct ClassDeclaration {
    std::string name;
    std::vector<FieldDeclaration> fields;
    std::vector<MethodDeclaration> methods;
    std::string inherits_from;
    int line = -1;
    int column = -1;
};

struct NewExpression {
    std::string class_name;
    std::vector<ASTNode> arguments;
    int line = -1;
    int column = -1;
};

struct MethodCallExpression {
    ASTNode object_expr;
    std::string method_name;
    std::vector<ASTNode> arguments;
    int line = -1;
    int column = -1;
};

struct FieldAccessExpression {
    ASTNode object_expr;
    std::string field_name;
    int line = -1;
    int column = -1;
};

struct TryStatement {
    ASTNode try_block;
    std::vector<CatchBlock> catch_blocks;
    int line = -1;
    int column = -1;
};

struct ThrowStatement {
    ASTNode expression;
    int line = -1;
    int column = -1;
};

struct Program { std::vector<ASTNode> statements; };

// The variant holding all node types
using ASTNodeVariant = std::variant<
    NumberLiteral, FloatLiteral, StringLiteral, BoolLiteral, NullLiteral, TypeLiteral,
    Identifier, ThisExpression,
    BinaryExpression, InExpression,
    ArrayLiteral, DictLiteral,
    VariableDeclaration, SetStatement, BreakStatement, ReturnStatement,
    Block, ExpressionStatement,
    CallExpression, MemberExpression, IndexExpression,
    TypeofExpression, HasattrExpression, ClassofExpression,
    IfStatement, WhileStatement, ForInStatement,
    FunctionDeclaration, ExternalDeclaration, DynamicFunctionDeclaration,
    FromImportStatement, SimpleImportStatement, LibcImportStatement,
    ClassDeclaration, NewExpression,
    MethodCallExpression, FieldAccessExpression,
    TryStatement, ThrowStatement,
    Program
>;

struct ASTNodeData : ASTNodeVariant {
    using ASTNodeVariant::variant;
};

// Helper to construct nodes
template<typename T, typename... Args>
ASTNode make_node(Args&&... args) {
    return std::make_unique<ASTNodeData>(T{std::forward<Args>(args)...});
}

// Helper to check node type
template<typename T>
bool is_node(const ASTNode& node) {
    return node && std::holds_alternative<T>(*node);
}

// Helper to get node data
template<typename T>
T& get_node(ASTNode& node) {
    return std::get<T>(*node);
}

template<typename T>
const T& get_node(const ASTNode& node) {
    return std::get<T>(*node);
}

// Helper to get line/column from any node that has them
inline int get_node_line(const ASTNode& node) {
    if (!node) return -1;
    return std::visit([](const auto& n) -> int {
        if constexpr (requires { n.line; }) return n.line;
        else return -1;
    }, *node);
}

inline int get_node_column(const ASTNode& node) {
    if (!node) return -1;
    return std::visit([](const auto& n) -> int {
        if constexpr (requires { n.column; }) return n.column;
        else return -1;
    }, *node);
}

ASTNode clone_node(const ASTNode& node);

namespace detail {

inline std::vector<ASTNode> clone_node_vector(const std::vector<ASTNode>& nodes) {
    std::vector<ASTNode> cloned;
    cloned.reserve(nodes.size());
    for (const auto& node : nodes) {
        cloned.push_back(clone_node(node));
    }
    return cloned;
}

inline Parameter clone_parameter(const Parameter& param) {
    return Parameter{
        param.name,
        param.param_type,
        clone_node(param.default_value),
        param.element_type,
    };
}

inline std::vector<Parameter> clone_parameter_vector(const std::vector<Parameter>& parameters) {
    std::vector<Parameter> cloned;
    cloned.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        cloned.push_back(clone_parameter(parameter));
    }
    return cloned;
}

inline FieldDeclaration clone_field_declaration(const FieldDeclaration& field) {
    return FieldDeclaration{
        field.name,
        field.type,
        clone_node(field.initializer),
        field.is_constructor_arg,
        field.element_type,
        field.line,
        field.column,
    };
}

inline std::vector<FieldDeclaration> clone_field_declarations(const std::vector<FieldDeclaration>& fields) {
    std::vector<FieldDeclaration> cloned;
    cloned.reserve(fields.size());
    for (const auto& field : fields) {
        cloned.push_back(clone_field_declaration(field));
    }
    return cloned;
}

inline MethodDeclaration clone_method_declaration(const MethodDeclaration& method) {
    return MethodDeclaration{
        method.name,
        clone_parameter_vector(method.params),
        method.return_type,
        clone_node(method.body),
        method.is_virtual,
        method.is_static,
        method.line,
        method.column,
    };
}

inline std::vector<MethodDeclaration> clone_method_declarations(const std::vector<MethodDeclaration>& methods) {
    std::vector<MethodDeclaration> cloned;
    cloned.reserve(methods.size());
    for (const auto& method : methods) {
        cloned.push_back(clone_method_declaration(method));
    }
    return cloned;
}

inline CatchBlock clone_catch_block(const CatchBlock& catch_block) {
    return CatchBlock{
        catch_block.exception_type,
        catch_block.identifier,
        clone_node(catch_block.body),
        catch_block.line,
        catch_block.column,
    };
}

inline std::vector<CatchBlock> clone_catch_blocks(const std::vector<CatchBlock>& catch_blocks) {
    std::vector<CatchBlock> cloned;
    cloned.reserve(catch_blocks.size());
    for (const auto& catch_block : catch_blocks) {
        cloned.push_back(clone_catch_block(catch_block));
    }
    return cloned;
}

}  // namespace detail

// Clone an ASTNode (deep copy) - needed for default parameter expansion
inline ASTNode clone_node(const ASTNode& node) {
    if (!node) {
        return nullptr;
    }

    return std::visit([](const auto& n) -> ASTNode {
        using T = std::decay_t<decltype(n)>;

        if constexpr (std::is_same_v<T, BinaryExpression>) {
            return make_node<BinaryExpression>(BinaryExpression{
                clone_node(n.left), n.op, clone_node(n.right),
            });
        } else if constexpr (std::is_same_v<T, InExpression>) {
            return make_node<InExpression>(InExpression{
                clone_node(n.item), clone_node(n.container), n.line, n.column,
            });
        } else if constexpr (std::is_same_v<T, ArrayLiteral>) {
            return make_node<ArrayLiteral>(ArrayLiteral{
                detail::clone_node_vector(n.elements),
            });
        } else if constexpr (std::is_same_v<T, DictLiteral>) {
            return make_node<DictLiteral>(DictLiteral{
                detail::clone_node_vector(n.keys),
                detail::clone_node_vector(n.values),
                n.key_type,
                n.value_type,
            });
        } else if constexpr (std::is_same_v<T, VariableDeclaration>) {
            return make_node<VariableDeclaration>(VariableDeclaration{
                n.type,
                n.name,
                clone_node(n.value),
                n.element_type,
                n.fixed_size,
                n.is_dynamic,
                n.key_type,
                n.value_type,
                n.line,
                n.column,
            });
        } else if constexpr (std::is_same_v<T, SetStatement>) {
            return make_node<SetStatement>(SetStatement{
                clone_node(n.target), clone_node(n.value), n.line, n.column,
            });
        } else if constexpr (std::is_same_v<T, ReturnStatement>) {
            return make_node<ReturnStatement>(ReturnStatement{
                clone_node(n.value),
            });
        } else if constexpr (std::is_same_v<T, Block>) {
            return make_node<Block>(Block{
                detail::clone_node_vector(n.statements),
            });
        } else if constexpr (std::is_same_v<T, ExpressionStatement>) {
            return make_node<ExpressionStatement>(ExpressionStatement{
                clone_node(n.expression),
            });
        } else if constexpr (std::is_same_v<T, CallExpression>) {
            return make_node<CallExpression>(CallExpression{
                clone_node(n.callee),
                detail::clone_node_vector(n.arguments),
                n.line,
                n.column,
            });
        } else if constexpr (std::is_same_v<T, MemberExpression>) {
            return make_node<MemberExpression>(MemberExpression{
                clone_node(n.object), n.property,
            });
        } else if constexpr (std::is_same_v<T, IndexExpression>) {
            return make_node<IndexExpression>(IndexExpression{
                clone_node(n.object), clone_node(n.index),
            });
        } else if constexpr (std::is_same_v<T, TypeofExpression>) {
            return make_node<TypeofExpression>(TypeofExpression{
                clone_node(n.argument),
            });
        } else if constexpr (std::is_same_v<T, HasattrExpression>) {
            return make_node<HasattrExpression>(HasattrExpression{
                clone_node(n.obj), n.attr_name, n.compile_time_result,
            });
        } else if constexpr (std::is_same_v<T, ClassofExpression>) {
            return make_node<ClassofExpression>(ClassofExpression{
                clone_node(n.argument),
            });
        } else if constexpr (std::is_same_v<T, IfStatement>) {
            return make_node<IfStatement>(IfStatement{
                clone_node(n.condition), clone_node(n.then_body), clone_node(n.else_body),
            });
        } else if constexpr (std::is_same_v<T, WhileStatement>) {
            return make_node<WhileStatement>(WhileStatement{
                clone_node(n.condition), clone_node(n.then_body),
            });
        } else if constexpr (std::is_same_v<T, ForInStatement>) {
            return make_node<ForInStatement>(ForInStatement{
                n.variable, clone_node(n.iterable), clone_node(n.body),
            });
        } else if constexpr (std::is_same_v<T, FunctionDeclaration>) {
            return make_node<FunctionDeclaration>(FunctionDeclaration{
                n.return_type,
                n.name,
                detail::clone_parameter_vector(n.parameters),
                clone_node(n.body),
            });
        } else if constexpr (std::is_same_v<T, ExternalDeclaration>) {
            return make_node<ExternalDeclaration>(ExternalDeclaration{
                n.return_type,
                n.name,
                detail::clone_parameter_vector(n.parameters),
            });
        } else if constexpr (std::is_same_v<T, DynamicFunctionDeclaration>) {
            return make_node<DynamicFunctionDeclaration>(DynamicFunctionDeclaration{
                n.name,
                detail::clone_parameter_vector(n.parameters),
                clone_node(n.body),
            });
        } else if constexpr (std::is_same_v<T, FromImportStatement>) {
            return make_node<FromImportStatement>(FromImportStatement{
                clone_node(n.module_path),
                n.symbols,
                n.is_wildcard,
            });
        } else if constexpr (std::is_same_v<T, ClassDeclaration>) {
            return make_node<ClassDeclaration>(ClassDeclaration{
                n.name,
                detail::clone_field_declarations(n.fields),
                detail::clone_method_declarations(n.methods),
                n.inherits_from,
                n.line,
                n.column,
            });
        } else if constexpr (std::is_same_v<T, NewExpression>) {
            return make_node<NewExpression>(NewExpression{
                n.class_name,
                detail::clone_node_vector(n.arguments),
                n.line,
                n.column,
            });
        } else if constexpr (std::is_same_v<T, MethodCallExpression>) {
            return make_node<MethodCallExpression>(MethodCallExpression{
                clone_node(n.object_expr),
                n.method_name,
                detail::clone_node_vector(n.arguments),
                n.line,
                n.column,
            });
        } else if constexpr (std::is_same_v<T, FieldAccessExpression>) {
            return make_node<FieldAccessExpression>(FieldAccessExpression{
                clone_node(n.object_expr), n.field_name, n.line, n.column,
            });
        } else if constexpr (std::is_same_v<T, TryStatement>) {
            return make_node<TryStatement>(TryStatement{
                clone_node(n.try_block),
                detail::clone_catch_blocks(n.catch_blocks),
                n.line,
                n.column,
            });
        } else if constexpr (std::is_same_v<T, ThrowStatement>) {
            return make_node<ThrowStatement>(ThrowStatement{
                clone_node(n.expression), n.line, n.column,
            });
        } else if constexpr (std::is_same_v<T, Program>) {
            return make_node<Program>(Program{
                detail::clone_node_vector(n.statements),
            });
        } else {
            return make_node<T>(n);
        }
    }, *node);
}
