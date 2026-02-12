#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ast_nodes.hpp"
#include "tokenizer.hpp"

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string file_path = "unknown");

    ASTNode parse_program();

private:
    struct TypeWithElement {
        std::string base_type;
        std::optional<std::string> element_type;
        std::optional<int> fixed_size;
    };

    struct TypeWithKeyValue {
        std::string base_type;
        std::optional<std::string> key_type;
        std::optional<std::string> value_type;
    };

    std::vector<Token> tokens;
    std::size_t position;
    std::string file_path;
    Token eof_token;

    const Token& current_token() const;
    void advance(std::size_t offset = 1);
    const Token* peek_token(std::size_t offset = 1) const;
    bool is_at_end() const;

    std::string get_position_info(const Token* token = nullptr) const;

    bool match(int token_type, const std::optional<std::string>& value = std::nullopt) const;
    bool peek_match(int token_type, const std::optional<std::string>& value = std::nullopt,
                    std::size_t offset = 0) const;
    void expect(int token_type, const std::optional<std::string>& value = std::nullopt);

    ASTNode parse_primary();
    ASTNode parse_call_member();
    ASTNode parse_function_call(ASTNode callee);

    ASTNode parse_multiplicative();
    ASTNode parse_additive();
    ASTNode parse_comparison();
    ASTNode parse_equality();
    ASTNode parse_in();
    ASTNode parse_and();
    ASTNode parse_or();
    ASTNode parse_expression();

    ASTNode parse_external();
    ASTNode parse_statement();
    ASTNode parse_expression_statement();
    ASTNode parse_set_statement();
    ASTNode parse_return_statement();
    ASTNode parse_block();
    ASTNode parse_if_statement();
    ASTNode parse_for_statement();
    ASTNode parse_while_statement();
    ASTNode parse_try_statement();
    CatchBlock parse_catch_block();
    ASTNode parse_throw_statement();
    ASTNode parse_break_statement();

    ASTNode parse_function_declaration();
    ASTNode parse_dynamic_function();

    TypeWithElement parse_type_with_element();
    TypeWithKeyValue parse_type_with_key_value();
    ASTNode parse_dict_literal();
    Parameter parse_parameter();

    ASTNode parse_variable_declaration();
    ASTNode parse_declaration();
    ASTNode parse_dynamic_declaration();
    ASTNode parse_import_statement();
    ASTNode parse_class_declaration();
    FieldDeclaration parse_field_declaration();
    MethodDeclaration parse_method_declaration();
};
