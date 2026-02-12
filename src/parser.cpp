#include "parser.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

std::string parser_token_type_name(int token_type) {
    if (token_type == -1) {
        return "EOF";
    }
    return token_type_name(token_type);
}

bool is_type_keyword(const Token& token) {
    if (token.type != T_KEYWORD) {
        return false;
    }
    static const std::unordered_set<std::string> kTypeKeywords = {
        "int", "float", "void", "array", "dict", "string", "bool",
    };
    return kTypeKeywords.count(token.value) > 0;
}

bool is_name_or_keyword(const Token& token) {
    return token.type == T_NAME || token.type == T_KEYWORD;
}

const std::unordered_set<std::string>& keyword_identifiers() {
    static const std::unordered_set<std::string> kKeywordIdentifiers = {
        "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "malloc", "free", "int", "str", "float", "read",
    };
    return kKeywordIdentifiers;
}

}  // namespace

Parser::Parser(std::vector<Token> tokens_in, std::string file_path_in)
    : tokens(std::move(tokens_in)),
      position(0),
      file_path(std::move(file_path_in)),
      eof_token(-1, "EOF", -1, -1) {}

const Token& Parser::current_token() const {
    if (position >= tokens.size()) {
        return eof_token;
    }
    return tokens[position];
}

void Parser::advance(std::size_t offset) {
    position += offset;
}

const Token* Parser::peek_token(std::size_t offset) const {
    if (position + offset >= tokens.size()) {
        return nullptr;
    }
    return &tokens[position + offset];
}

bool Parser::is_at_end() const {
    return position >= tokens.size();
}

std::string Parser::get_position_info(const Token* token) const {
    const Token& tok = token ? *token : current_token();
    if (tok.line >= 0) {
        std::string info = " at line " + std::to_string(tok.line);
        if (tok.column >= 0) {
            info += ", column " + std::to_string(tok.column);
        }
        return info;
    }
    return "";
}

bool Parser::match(int token_type, const std::optional<std::string>& value) const {
    const Token& tok = current_token();
    if (tok.type != token_type) {
        return false;
    }
    if (value.has_value() && tok.value != *value) {
        return false;
    }
    return true;
}

bool Parser::peek_match(int token_type, const std::optional<std::string>& value, std::size_t offset) const {
    if (position + offset >= tokens.size()) {
        return false;
    }
    const Token& tok = tokens[position + offset];
    if (tok.type != token_type) {
        return false;
    }
    if (value.has_value() && tok.value != *value) {
        return false;
    }
    return true;
}

void Parser::expect(int token_type, const std::optional<std::string>& value) {
    const Token& current = current_token();
    if (current.type == token_type && (!value.has_value() || current.value == *value)) {
        advance();
        return;
    }

    const std::string pos_info = get_position_info(&current);
    if (value.has_value()) {
        throw CompilerError(
            "Expected '" + *value + "'" + pos_info + " but found '" + current.value + "' (" +
                parser_token_type_name(current.type) + ")",
            "ERROR", file_path);
    }

    throw CompilerError(
        "Expected " + parser_token_type_name(token_type) + pos_info + " but found '" + current.value + "' (" +
            parser_token_type_name(current.type) + ")",
        "ERROR", file_path);
}

ASTNode Parser::parse_primary() {
    if (match(T_INT_LITERAL)) {
        const Token token = current_token();
        advance();
        return make_node<NumberLiteral>(NumberLiteral{std::stoi(token.value), token.line, token.column});
    }

    if (match(T_FLOAT_LITERAL)) {
        const Token token = current_token();
        advance();
        return make_node<FloatLiteral>(FloatLiteral{std::stod(token.value), token.line, token.column});
    }

    if (match(T_STRING)) {
        const Token token = current_token();
        advance();
        return make_node<StringLiteral>(StringLiteral{token.value, token.line, token.column});
    }

    if (match(T_NAME) || (match(T_KEYWORD) && keyword_identifiers().count(current_token().value) > 0)) {
        const Token token = current_token();
        advance();
        ASTNode literal = make_node<Identifier>(Identifier{token.value, token.line, token.column});

        if (match(T_SYMBOL, "(")) {
            return parse_function_call(std::move(literal));
        }

        return literal;
    }

    if (match(T_KEYWORD, "null")) {
        advance();
        return make_node<NullLiteral>(NullLiteral{});
    }

    if (match(T_KEYWORD, "typeof")) {
        advance();
        expect(T_SYMBOL, "(");
        ASTNode node = parse_expression();
        expect(T_SYMBOL, ")");
        return make_node<TypeofExpression>(TypeofExpression{std::move(node)});
    }

    if (match(T_KEYWORD, "hasattr")) {
        advance();
        expect(T_SYMBOL, "(");
        ASTNode obj = parse_expression();
        expect(T_SYMBOL, ",");

        if (!match(T_STRING)) {
            throw CompilerError(
                "hasattr requires a string literal for attribute name" + get_position_info(),
                "ERROR", file_path);
        }

        const std::string attr_name = current_token().value;
        advance();
        expect(T_SYMBOL, ")");
        return make_node<HasattrExpression>(HasattrExpression{std::move(obj), attr_name, false});
    }

    if (match(T_KEYWORD, "classof")) {
        advance();
        expect(T_SYMBOL, "(");
        ASTNode node = parse_expression();
        expect(T_SYMBOL, ")");
        return make_node<ClassofExpression>(ClassofExpression{std::move(node)});
    }

    if (match(T_KEYWORD, "this")) {
        const Token token = current_token();
        advance();
        return make_node<ThisExpression>(ThisExpression{token.line, token.column});
    }

    if (match(T_KEYWORD, "new")) {
        advance();
        const Token class_token = current_token();
        const std::string class_name = class_token.value;
        expect(T_NAME);
        expect(T_SYMBOL, "(");

        std::vector<ASTNode> args;
        if (!match(T_SYMBOL, ")")) {
            args.push_back(parse_expression());
            while (match(T_SYMBOL, ",")) {
                advance();
                args.push_back(parse_expression());
            }
        }
        expect(T_SYMBOL, ")");
        return make_node<NewExpression>(NewExpression{class_name, std::move(args), class_token.line, class_token.column});
    }

    if (match(T_KEYWORD, "int") || match(T_KEYWORD, "float") || match(T_KEYWORD, "string") ||
        match(T_KEYWORD, "array") || match(T_KEYWORD, "dict") || match(T_KEYWORD, "bool") ||
        match(T_KEYWORD, "void")) {
        const std::string value = current_token().value;
        advance();
        return make_node<TypeLiteral>(TypeLiteral{value});
    }

    if (match(T_SYMBOL, "(")) {
        advance();
        ASTNode node = parse_expression();
        expect(T_SYMBOL, ")");
        return node;
    }

    if (match(T_SYMBOL, "[")) {
        advance();
        std::vector<ASTNode> elements;
        if (!match(T_SYMBOL, "]")) {
            elements.push_back(parse_expression());
            while (match(T_SYMBOL, ",")) {
                advance();
                elements.push_back(parse_expression());
            }
        }
        expect(T_SYMBOL, "]");
        return make_node<ArrayLiteral>(ArrayLiteral{std::move(elements)});
    }

    if (match(T_SYMBOL, "{")) {
        return parse_dict_literal();
    }

    if (match(T_KEYWORD, "true")) {
        advance();
        return make_node<BoolLiteral>(BoolLiteral{true});
    }

    if (match(T_KEYWORD, "false")) {
        advance();
        return make_node<BoolLiteral>(BoolLiteral{false});
    }

    const Token& current = current_token();
    throw CompilerError(
        "Unexpected token " + parser_token_type_name(current.type) + " '" + current.value + "'" +
            get_position_info(&current),
        "ERROR", file_path);
}

ASTNode Parser::parse_call_member() {
    ASTNode base = parse_primary();
    while (match(T_SYMBOL, "(") || match(T_SYMBOL, ".") || match(T_SYMBOL, "[")) {
        if (match(T_SYMBOL, "(")) {
            advance();
            std::vector<ASTNode> args;
            if (!match(T_SYMBOL, ")")) {
                args.push_back(parse_expression());
                while (match(T_SYMBOL, ",")) {
                    advance();
                    args.push_back(parse_expression());
                }
            }
            expect(T_SYMBOL, ")");
            const int line = get_node_line(base);
            const int column = get_node_column(base);
            base = make_node<CallExpression>(
                CallExpression{std::move(base), std::move(args), line, column});
        } else if (match(T_SYMBOL, ".")) {
            advance();
            if (!(match(T_NAME) || match(T_KEYWORD))) {
                const Token& current = current_token();
                throw CompilerError(
                    "Invalid member property: '" + current.value + "'" + get_position_info(&current) +
                        " - expected NAME or KEYWORD",
                    "ERROR", file_path);
            }

            const std::string member_property = current_token().value;
            advance();
            base = make_node<MemberExpression>(MemberExpression{std::move(base), member_property});

            if (match(T_SYMBOL, "(")) {
                advance();
                std::vector<ASTNode> args;
                if (!match(T_SYMBOL, ")")) {
                    args.push_back(parse_expression());
                    while (match(T_SYMBOL, ",")) {
                        advance();
                        args.push_back(parse_expression());
                    }
                }
                expect(T_SYMBOL, ")");
                const int line = get_node_line(base);
                const int column = get_node_column(base);
                base = make_node<CallExpression>(
                    CallExpression{std::move(base), std::move(args), line, column});
            }
        } else if (match(T_SYMBOL, "[")) {
            advance();
            ASTNode index = parse_expression();
            expect(T_SYMBOL, "]");
            base = make_node<IndexExpression>(
                IndexExpression{std::move(base), std::move(index)});
        }
    }
    return base;
}

ASTNode Parser::parse_function_call(ASTNode callee) {
    advance();
    std::vector<ASTNode> args;
    if (!match(T_SYMBOL, ")")) {
        args.push_back(parse_expression());
        while (match(T_SYMBOL, ",")) {
            advance();
            args.push_back(parse_expression());
        }
    }
    expect(T_SYMBOL, ")");

    const int line = get_node_line(callee);
    const int column = get_node_column(callee);
    return make_node<CallExpression>(CallExpression{std::move(callee), std::move(args), line, column});
}

ASTNode Parser::parse_multiplicative() {
    ASTNode left = parse_call_member();
    while (match(T_SYMBOL, "*") || match(T_SYMBOL, "/")) {
        const Token op = current_token();
        advance();
        ASTNode right = parse_call_member();
        left = make_node<BinaryExpression>(
            BinaryExpression{std::move(left), op, std::move(right)});
    }
    return left;
}

ASTNode Parser::parse_additive() {
    ASTNode left = parse_multiplicative();
    while (match(T_SYMBOL, "+") || match(T_SYMBOL, "-")) {
        const Token op = current_token();
        advance();
        ASTNode right = parse_multiplicative();
        left = make_node<BinaryExpression>(
            BinaryExpression{std::move(left), op, std::move(right)});
    }
    return left;
}

ASTNode Parser::parse_comparison() {
    ASTNode left = parse_additive();
    while (match(T_SYMBOL, "<") || match(T_SYMBOL, ">") || match(T_SYMBOL, ">=") ||
           match(T_SYMBOL, "<=")) {
        const Token op = current_token();
        advance();
        ASTNode right = parse_additive();
        left = make_node<BinaryExpression>(
            BinaryExpression{std::move(left), op, std::move(right)});
    }
    return left;
}

ASTNode Parser::parse_equality() {
    ASTNode left = parse_comparison();
    while (match(T_SYMBOL, "==") || match(T_SYMBOL, "!=")) {
        const Token op = current_token();
        advance();
        ASTNode right = parse_comparison();
        left = make_node<BinaryExpression>(
            BinaryExpression{std::move(left), op, std::move(right)});
    }
    return left;
}

ASTNode Parser::parse_in() {
    ASTNode left = parse_equality();
    while (match(T_KEYWORD, "in")) {
        const Token op = current_token();
        advance();
        ASTNode right = parse_equality();
        left = make_node<InExpression>(
            InExpression{std::move(left), std::move(right), op.line, op.column});
    }
    return left;
}

ASTNode Parser::parse_and() {
    ASTNode left = parse_in();
    while (match(T_SYMBOL, "&&")) {
        const Token op = current_token();
        advance();
        ASTNode right = parse_in();
        left = make_node<BinaryExpression>(
            BinaryExpression{std::move(left), op, std::move(right)});
    }
    return left;
}

ASTNode Parser::parse_or() {
    ASTNode left = parse_and();
    while (match(T_SYMBOL, "||")) {
        const Token op = current_token();
        advance();
        ASTNode right = parse_and();
        left = make_node<BinaryExpression>(
            BinaryExpression{std::move(left), op, std::move(right)});
    }
    return left;
}

ASTNode Parser::parse_expression() {
    return parse_or();
}

ASTNode Parser::parse_external() {
    advance();
    const std::string return_type = current_token().value;
    advance();

    const std::string func_name = current_token().value;
    expect(T_NAME);

    expect(T_SYMBOL, "(");
    std::vector<Parameter> params;
    if (!match(T_SYMBOL, ")")) {
        params.push_back(parse_parameter());
        while (match(T_SYMBOL, ",")) {
            advance();
            params.push_back(parse_parameter());
        }
    }
    expect(T_SYMBOL, ")");

    return make_node<ExternalDeclaration>(
        ExternalDeclaration{return_type, func_name, std::move(params)});
}

ASTNode Parser::parse_statement() {
    if (match(T_KEYWORD, "dynamic")) {
        return parse_dynamic_declaration();
    }
    if (match(T_KEYWORD, "set")) {
        return parse_set_statement();
    }
    if (match(T_KEYWORD, "return")) {
        return parse_return_statement();
    }
    if (match(T_KEYWORD, "if")) {
        return parse_if_statement();
    }
    if (match(T_KEYWORD, "for")) {
        return parse_for_statement();
    }
    if (match(T_KEYWORD, "func")) {
        return parse_dynamic_function();
    }
    if (match(T_KEYWORD, "external")) {
        return parse_external();
    }

    if (is_type_keyword(current_token())) {
        const Token* next = peek_token();
        if (next && next->type == T_SYMBOL && next->value == "(") {
            return parse_expression_statement();
        }
        if (next && next->type == T_NAME) {
            return parse_declaration();
        }
        if (next && next->type == T_SYMBOL && next->value == "<") {
            return parse_declaration();
        }
        if (next && next->type == T_SYMBOL && next->value == "[") {
            return parse_declaration();
        }
        return parse_expression_statement();
    }

    if (current_token().type == T_NAME) {
        const Token* next = peek_token();
        if (next && next->type == T_NAME) {
            return parse_declaration();
        }
    }

    if (match(T_KEYWORD, "from") || match(T_KEYWORD, "use")) {
        return parse_import_statement();
    }
    if (match(T_KEYWORD, "while")) {
        return parse_while_statement();
    }
    if (match(T_KEYWORD, "class")) {
        return parse_class_declaration();
    }
    if (match(T_KEYWORD, "try")) {
        return parse_try_statement();
    }
    if (match(T_KEYWORD, "throw")) {
        return parse_throw_statement();
    }
    if (match(T_KEYWORD, "break")) {
        return parse_break_statement();
    }

    return parse_expression_statement();
}

ASTNode Parser::parse_expression_statement() {
    ASTNode expression = parse_expression();
    return make_node<ExpressionStatement>(ExpressionStatement{std::move(expression)});
}

ASTNode Parser::parse_set_statement() {
    const Token start = current_token();
    expect(T_KEYWORD, "set");
    ASTNode target = parse_call_member();

    if (match(T_SYMBOL, "=")) {
        expect(T_SYMBOL, "=");
        ASTNode value = parse_expression();
        return make_node<SetStatement>(
            SetStatement{std::move(target), std::move(value), start.line, start.column});
    }

    return make_node<ExpressionStatement>(ExpressionStatement{std::move(target)});
}

ASTNode Parser::parse_return_statement() {
    advance();
    ASTNode value = nullptr;
    if (!match(T_SYMBOL, "}") && !is_at_end()) {
        value = parse_expression();
    }
    return make_node<ReturnStatement>(ReturnStatement{std::move(value)});
}

ASTNode Parser::parse_block() {
    expect(T_SYMBOL, "{");
    std::vector<ASTNode> statements;

    while (!match(T_SYMBOL, "}")) {
        if (is_at_end()) {
            throw CompilerError("Unterminated block - expected '}' but reached end of file", "ERROR",
                                file_path);
        }

        const std::size_t start_pos = position;
        statements.push_back(parse_statement());
        if (position == start_pos) {
            const Token& tok = current_token();
            throw CompilerError(
                "Unexpected token '" + tok.value + "' at line " + std::to_string(tok.line) +
                    ", column " + std::to_string(tok.column),
                "ERROR", file_path);
        }
    }

    expect(T_SYMBOL, "}");
    return make_node<Block>(Block{std::move(statements)});
}

ASTNode Parser::parse_if_statement() {
    advance();
    expect(T_SYMBOL, "(");
    ASTNode condition = parse_expression();
    expect(T_SYMBOL, ")");
    ASTNode body = parse_block();

    ASTNode else_body = nullptr;
    if (match(T_KEYWORD, "elif")) {
        ASTNode nested_if = parse_if_statement();
        std::vector<ASTNode> else_statements;
        else_statements.push_back(std::move(nested_if));
        else_body = make_node<Block>(Block{std::move(else_statements)});
    } else if (match(T_KEYWORD, "else")) {
        advance();
        else_body = parse_block();
    }

    return make_node<IfStatement>(
        IfStatement{std::move(condition), std::move(body), std::move(else_body)});
}

ASTNode Parser::parse_for_statement() {
    advance();
    expect(T_SYMBOL, "(");

    const std::string loop_var_name = current_token().value;
    expect(T_NAME);
    expect(T_KEYWORD, "in");

    ASTNode iterable = parse_expression();
    expect(T_SYMBOL, ")");
    ASTNode body = parse_block();

    return make_node<ForInStatement>(
        ForInStatement{loop_var_name, std::move(iterable), std::move(body)});
}

ASTNode Parser::parse_while_statement() {
    advance();
    expect(T_SYMBOL, "(");
    ASTNode loop_condition = parse_expression();
    expect(T_SYMBOL, ")");
    ASTNode body = parse_block();
    return make_node<WhileStatement>(
        WhileStatement{std::move(loop_condition), std::move(body)});
}

ASTNode Parser::parse_try_statement() {
    const Token start = current_token();
    expect(T_KEYWORD, "try");
    ASTNode try_block = parse_block();

    std::vector<CatchBlock> catch_blocks;
    while (match(T_KEYWORD, "catch") || match(T_KEYWORD, "except")) {
        catch_blocks.push_back(parse_catch_block());
    }

    if (catch_blocks.empty()) {
        throw CompilerError("try statement must have at least one catch block", "ERROR", file_path);
    }

    return make_node<TryStatement>(TryStatement{
        std::move(try_block), std::move(catch_blocks), start.line, start.column,
    });
}

CatchBlock Parser::parse_catch_block() {
    const Token start = current_token();

    if (match(T_KEYWORD, "catch") || match(T_KEYWORD, "except")) {
        advance();
    }

    std::string exception_type;
    std::string identifier;

    if (is_name_or_keyword(current_token()) && current_token().value != "{") {
        exception_type = current_token().value;
        advance();
    }

    if (match(T_SYMBOL, "(")) {
        advance();
        if (current_token().type == T_NAME) {
            identifier = current_token().value;
            advance();
        } else {
            throw CompilerError("Expected exception variable name in catch block", "ERROR", file_path);
        }
        expect(T_SYMBOL, ")");
    }

    ASTNode body = parse_block();
    return CatchBlock{exception_type, identifier, std::move(body), start.line, start.column};
}

ASTNode Parser::parse_throw_statement() {
    const Token start = current_token();
    expect(T_KEYWORD, "throw");

    ASTNode expression = nullptr;
    if (!match(T_SYMBOL, "}") && !is_at_end() && !match(T_KEYWORD, "catch") && !match(T_KEYWORD, "else")) {
        expression = parse_expression();
    }

    return make_node<ThrowStatement>(
        ThrowStatement{std::move(expression), start.line, start.column});
}

ASTNode Parser::parse_break_statement() {
    const Token start = current_token();
    expect(T_KEYWORD, "break");
    return make_node<BreakStatement>(BreakStatement{start.line, start.column});
}

ASTNode Parser::parse_function_declaration() {
    const std::string return_type = current_token().value;
    advance();

    const std::string func_name = current_token().value;
    expect(T_NAME);
    expect(T_SYMBOL, "(");

    std::vector<Parameter> params;
    if (!match(T_SYMBOL, ")")) {
        params.push_back(parse_parameter());
        while (match(T_SYMBOL, ",")) {
            advance();
            params.push_back(parse_parameter());
        }
    }
    expect(T_SYMBOL, ")");

    ASTNode body = parse_block();
    return make_node<FunctionDeclaration>(FunctionDeclaration{
        return_type,
        func_name,
        std::move(params),
        std::move(body),
    });
}

ASTNode Parser::parse_dynamic_function() {
    expect(T_KEYWORD, "func");

    const std::string func_name = current_token().value;
    expect(T_NAME);

    expect(T_SYMBOL, "(");
    std::vector<Parameter> params;
    if (!match(T_SYMBOL, ")")) {
        params.push_back(parse_parameter());
        while (match(T_SYMBOL, ",")) {
            advance();
            params.push_back(parse_parameter());
        }
    }
    expect(T_SYMBOL, ")");

    expect(T_SYMBOL, "{");
    std::vector<ASTNode> statements;
    while (!match(T_SYMBOL, "}")) {
        statements.push_back(parse_statement());
    }
    expect(T_SYMBOL, "}");

    ASTNode body = make_node<Block>(Block{std::move(statements)});
    return make_node<DynamicFunctionDeclaration>(
        DynamicFunctionDeclaration{func_name, std::move(params), std::move(body)});
}

Parser::TypeWithElement Parser::parse_type_with_element() {
    std::string base_type = current_token().value;
    const std::string original_type = base_type;
    advance();

    std::optional<std::string> element_type;
    std::optional<int> fixed_size;

    if (base_type == "array" && match(T_SYMBOL, "<")) {
        advance();
        if (match(T_KEYWORD) || match(T_NAME)) {
            element_type = current_token().value;
        } else {
            const Token& current = current_token();
            throw CompilerError(
                "Expected type name after '<'" + get_position_info(&current) + " but found '" +
                    current.value + "'",
                "ERROR", file_path);
        }
        advance();
        expect(T_SYMBOL, ">");
    }

    if (match(T_SYMBOL, "[")) {
        advance();
        if (match(T_SYMBOL, "]")) {
            advance();
            if (base_type == "array") {
                if (!element_type.has_value()) {
                    const Token& current = current_token();
                    throw CompilerError(
                        "Array type requires an element type (e.g. array<int>)" +
                            get_position_info(&current),
                        "ERROR", file_path);
                }
            } else {
                element_type = original_type;
                base_type = "array";
            }
        } else if (match(T_INT_LITERAL)) {
            const Token size_token = current_token();
            const int parsed_size = std::stoi(size_token.value);
            if (parsed_size <= 0) {
                throw CompilerError(
                    "Array size must be positive" + get_position_info(&size_token),
                    "ERROR", file_path);
            }
            fixed_size = parsed_size;
            advance();
            expect(T_SYMBOL, "]");

            if (base_type == "array") {
                if (!element_type.has_value()) {
                    throw CompilerError(
                        "Fixed-size array requires an element type (e.g. array<int>[8])" +
                            get_position_info(&size_token),
                        "ERROR", file_path);
                }
            } else {
                element_type = original_type;
                base_type = "array";
            }
        } else {
            const Token& current = current_token();
            throw CompilerError(
                "Expected integer literal or ']' for array type" + get_position_info(&current) +
                    " but found '" + current.value + "'",
                "ERROR", file_path);
        }
    }

    return TypeWithElement{base_type, element_type, fixed_size};
}

Parser::TypeWithKeyValue Parser::parse_type_with_key_value() {
    std::string base_type = current_token().value;
    advance();

    std::optional<std::string> key_type;
    std::optional<std::string> value_type;

    if (base_type == "dict" && match(T_SYMBOL, "<")) {
        advance();

        if (match(T_KEYWORD) || match(T_NAME)) {
            key_type = current_token().value;
        } else {
            const Token& current = current_token();
            throw CompilerError(
                "Expected key type name after '<'" + get_position_info(&current) + " but found '" +
                    current.value + "'",
                "ERROR", file_path);
        }
        advance();

        expect(T_SYMBOL, ",");

        if (match(T_KEYWORD) || match(T_NAME)) {
            value_type = current_token().value;
        } else {
            const Token& current = current_token();
            throw CompilerError(
                "Expected value type name after ','" + get_position_info(&current) + " but found '" +
                    current.value + "'",
                "ERROR", file_path);
        }
        advance();

        expect(T_SYMBOL, ">");
    }

    return TypeWithKeyValue{base_type, key_type, value_type};
}

ASTNode Parser::parse_dict_literal() {
    expect(T_SYMBOL, "{");

    std::vector<ASTNode> keys;
    std::vector<ASTNode> values;

    if (!match(T_SYMBOL, "}")) {
        while (true) {
            keys.push_back(parse_expression());
            expect(T_SYMBOL, ":");
            values.push_back(parse_expression());

            if (!match(T_SYMBOL, ",")) {
                break;
            }
            advance();
        }
    }

    expect(T_SYMBOL, "}");
    return make_node<DictLiteral>(DictLiteral{std::move(keys), std::move(values), "", ""});
}

Parameter Parser::parse_parameter() {
    TypeWithElement type_info = parse_type_with_element();
    if (type_info.fixed_size.has_value()) {
        const Token& current = current_token();
        throw CompilerError(
            "Fixed-size arrays are only supported for variable declarations" +
                get_position_info(&current),
            "ERROR", file_path);
    }

    const std::string param_name = current_token().value;
    expect(T_NAME);

    ASTNode default_value = nullptr;
    if (match(T_SYMBOL, "=")) {
        advance();
        default_value = parse_expression();
    }

    return Parameter{
        param_name,
        type_info.base_type,
        std::move(default_value),
        type_info.element_type.value_or(""),
    };
}

ASTNode Parser::parse_variable_declaration(bool is_dynamic) {
    if (current_token().value == "dict" && peek_token(1) && peek_token(1)->value == "<") {
        TypeWithKeyValue dict_type = parse_type_with_key_value();

        const Token var_name_token = current_token();
        const std::string var_name = var_name_token.value;
        expect(T_NAME);

        ASTNode value = nullptr;
        if (match(T_SYMBOL, "=")) {
            advance();
            value = parse_expression();
        }

        return make_node<VariableDeclaration>(VariableDeclaration{
            dict_type.base_type,
            var_name,
            std::move(value),
            "",
            -1,
            false,
            dict_type.key_type.value_or(""),
            dict_type.value_type.value_or(""),
            var_name_token.line,
            var_name_token.column,
        });
    }

    TypeWithElement type_info = parse_type_with_element();

    const Token var_name_token = current_token();
    const std::string var_name = var_name_token.value;
    expect(T_NAME);

    ASTNode value = nullptr;
    if (match(T_SYMBOL, "=")) {
        advance();
        value = parse_expression();
    }

    return make_node<VariableDeclaration>(VariableDeclaration{
        type_info.base_type,
        var_name,
        std::move(value),
        type_info.element_type.value_or(""),
        type_info.fixed_size.value_or(-1),
        is_dynamic,
        "",
        "",
        var_name_token.line,
        var_name_token.column,
    });
}

ASTNode Parser::parse_declaration() {
    std::size_t offset = 1;

    if (current_token().value == "dict" && peek_token(1) && peek_token(1)->value == "<") {
        offset = 6;
    } else if (current_token().value == "array" && peek_token(1) && peek_token(1)->value == "<") {
        offset = 4;
    }

    const Token* next_token = peek_token(offset + 1);
    if (next_token && next_token->type == T_SYMBOL && next_token->value == "(") {
        return parse_function_declaration();
    }
    return parse_variable_declaration(false);
}

ASTNode Parser::parse_dynamic_declaration() {
    const Token start = current_token();
    expect(T_KEYWORD, "dynamic");

    if (!match(T_KEYWORD, "array")) {
        const Token& current = current_token();
        throw CompilerError(
            "Expected 'array' after 'dynamic'" + get_position_info(&current) + " but found '" +
                current.value + "'",
            "ERROR", file_path);
    }

    ASTNode declaration = parse_variable_declaration(true);
    if (!declaration || !is_node<VariableDeclaration>(declaration)) {
        throw CompilerError(
            "Invalid dynamic declaration" + get_position_info(&start),
            "ERROR", file_path);
    }

    auto& variable = get_node<VariableDeclaration>(declaration);
    if (variable.type != "array") {
        throw CompilerError(
            "dynamic declarations only support arrays" + get_position_info(&start),
            "ERROR", file_path);
    }
    if (variable.fixed_size > 0) {
        throw CompilerError(
            "dynamic array cannot have fixed stack size" + get_position_info(&start),
            "ERROR", file_path);
    }
    variable.is_dynamic = true;

    return declaration;
}

ASTNode Parser::parse_import_statement() {
    if (match(T_KEYWORD, "from")) {
        advance();
        ASTNode module_path = parse_expression();
        expect(T_KEYWORD, "use");

        if (match(T_SYMBOL, "*")) {
            advance();
            if (is_node<Identifier>(module_path) && get_node<Identifier>(module_path).name == "libc") {
                throw CompilerError("Wildcard import not allowed for libc module", "ERROR", file_path);
            }
            return make_node<FromImportStatement>(FromImportStatement{
                std::move(module_path),
                {},
                true,
            });
        }

        std::vector<std::string> symbols;
        std::string symbol_name = current_token().value;
        expect(T_NAME);
        symbols.push_back(symbol_name);

        while (match(T_SYMBOL, ",")) {
            advance();
            symbol_name = current_token().value;
            expect(T_NAME);
            symbols.push_back(symbol_name);
        }

        if (is_node<Identifier>(module_path) && get_node<Identifier>(module_path).name == "libc") {
            return make_node<LibcImportStatement>(LibcImportStatement{std::move(symbols)});
        }

        return make_node<FromImportStatement>(FromImportStatement{
            std::move(module_path),
            std::move(symbols),
            false,
        });
    }

    if (match(T_KEYWORD, "use")) {
        advance();
        const std::string module_name = current_token().value;
        expect(T_NAME);

        std::string alias;
        if (match(T_KEYWORD, "as")) {
            advance();
            alias = current_token().value;
            expect(T_NAME);
        }

        return make_node<SimpleImportStatement>(SimpleImportStatement{module_name, alias});
    }

    throw CompilerError("Invalid import statement", "ERROR", file_path);
}

ASTNode Parser::parse_class_declaration() {
    const Token class_token = current_token();
    advance();

    const std::string class_name = current_token().value;
    expect(T_NAME);

    std::string inherits_from;
    if (match(T_NAME, "inherits") || match(T_NAME, "extends")) {
        advance();
        inherits_from = current_token().value;
        expect(T_NAME);
    }

    expect(T_SYMBOL, "{");

    std::vector<FieldDeclaration> fields;
    std::vector<MethodDeclaration> methods;

    while (!match(T_SYMBOL, "}")) {
        if (is_at_end()) {
            throw CompilerError("Unterminated class declaration", "ERROR", file_path);
        }

        const std::size_t saved_pos = position;

        const bool has_static = peek_match(T_KEYWORD, "static");
        const bool has_virtual = !has_static && peek_match(T_KEYWORD, "virtual");

        std::size_t peek_pos = position;
        if (has_static || has_virtual) {
            ++peek_pos;
        }

        bool type_match = false;
        bool is_func = false;

        if (peek_pos < tokens.size()) {
            Token token = tokens[peek_pos];

            if (token.type == T_KEYWORD && token.value == "arg") {
                ++peek_pos;
                if (peek_pos < tokens.size()) {
                    token = tokens[peek_pos];
                }
            }

            if (token.type == T_KEYWORD &&
                (token.value == "int" || token.value == "float" || token.value == "void" ||
                 token.value == "string" || token.value == "bool" || token.value == "array" ||
                 token.value == "dict")) {
                type_match = true;
                ++peek_pos;

                if (token.value == "array" && peek_pos < tokens.size() &&
                    tokens[peek_pos].type == T_SYMBOL && tokens[peek_pos].value == "<") {
                    ++peek_pos;
                    if (peek_pos < tokens.size()) {
                        ++peek_pos;
                    }
                    if (peek_pos < tokens.size() && tokens[peek_pos].type == T_SYMBOL &&
                        tokens[peek_pos].value == ">") {
                        ++peek_pos;
                    }
                } else if (token.value == "dict" && peek_pos < tokens.size() &&
                           tokens[peek_pos].type == T_SYMBOL && tokens[peek_pos].value == "<") {
                    ++peek_pos;
                    if (peek_pos < tokens.size()) {
                        ++peek_pos;
                    }
                    if (peek_pos < tokens.size() && tokens[peek_pos].type == T_SYMBOL &&
                        tokens[peek_pos].value == ",") {
                        ++peek_pos;
                    }
                    if (peek_pos < tokens.size()) {
                        ++peek_pos;
                    }
                    if (peek_pos < tokens.size() && tokens[peek_pos].type == T_SYMBOL &&
                        tokens[peek_pos].value == ">") {
                        ++peek_pos;
                    }
                }
            } else if (token.type == T_NAME) {
                type_match = true;
                ++peek_pos;
            } else if (token.type == T_KEYWORD && token.value == "func") {
                is_func = true;
                ++peek_pos;
            }

            if (type_match || is_func) {
                if (peek_pos < tokens.size() && tokens[peek_pos].type == T_NAME) {
                    ++peek_pos;
                    if (peek_pos < tokens.size() && tokens[peek_pos].type == T_SYMBOL &&
                        tokens[peek_pos].value == "(") {
                        methods.push_back(parse_method_declaration());
                    } else {
                        fields.push_back(parse_field_declaration());
                    }
                    continue;
                }
            }
        }

        position = saved_pos;
        const Token& current = current_token();
        throw CompilerError(
            "Expected field or method declaration in class" + get_position_info(&current), "ERROR",
            file_path);
    }

    expect(T_SYMBOL, "}");
    return make_node<ClassDeclaration>(ClassDeclaration{
        class_name,
        std::move(fields),
        std::move(methods),
        inherits_from,
        class_token.line,
        class_token.column,
    });
}

FieldDeclaration Parser::parse_field_declaration() {
    bool is_constructor_arg = false;
    if (match(T_KEYWORD, "arg")) {
        advance();
        is_constructor_arg = true;
    }

    const Token type_token = current_token();
    TypeWithElement type_info = parse_type_with_element();

    const std::string field_name = current_token().value;
    expect(T_NAME);

    ASTNode initializer = nullptr;
    if (match(T_SYMBOL, "=")) {
        advance();
        initializer = parse_expression();
    }

    return FieldDeclaration{
        field_name,
        type_info.base_type,
        std::move(initializer),
        is_constructor_arg,
        type_info.element_type.value_or(""),
        type_token.line,
        type_token.column,
    };
}

MethodDeclaration Parser::parse_method_declaration() {
    bool is_static = false;
    bool is_virtual = false;

    if (match(T_KEYWORD, "static")) {
        advance();
        is_static = true;
    } else if (match(T_KEYWORD, "virtual")) {
        advance();
        is_virtual = true;
    }

    const Token start_token = current_token();

    std::string return_type;
    if (match(T_KEYWORD, "func")) {
        advance();
    } else {
        return_type = current_token().value;
        if (current_token().type == T_KEYWORD || current_token().type == T_NAME) {
            advance();
        } else {
            throw CompilerError(
                "Expected return type but found '" + current_token().value + "' (" +
                    parser_token_type_name(current_token().type) + ")",
                "ERROR", file_path);
        }
    }

    const std::string method_name = current_token().value;
    expect(T_NAME);

    expect(T_SYMBOL, "(");
    std::vector<Parameter> params;
    if (!match(T_SYMBOL, ")")) {
        params.push_back(parse_parameter());
        while (match(T_SYMBOL, ",")) {
            advance();
            params.push_back(parse_parameter());
        }
    }
    expect(T_SYMBOL, ")");

    ASTNode body = parse_block();
    return MethodDeclaration{
        method_name,
        std::move(params),
        return_type,
        std::move(body),
        is_virtual,
        is_static,
        start_token.line,
        start_token.column,
    };
}

ASTNode Parser::parse_program() {
    std::vector<ASTNode> statements;
    while (!is_at_end()) {
        statements.push_back(parse_statement());
    }
    return make_node<Program>(Program{std::move(statements)});
}
