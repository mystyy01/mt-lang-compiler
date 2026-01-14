from tokenizer import Token, CompilerError
from ast_nodes import *
class Parser:
    def __init__(self, tokens: list[Token], file_path=None):
        self.tokens = tokens
        self.position = 0
        self.file_path = file_path or "unknown"
    def current_token(self):
        if self.position >= len(self.tokens):
            return Token("EOF", "")
        return self.tokens[self.position]
    def advance(self, offset = 1):
        self.position += offset
    def peek_token(self, value=1):
        if self.position + value < len(self.tokens):
            return self.tokens[self.position+value]
        return None
    def is_at_end(self):
        if self.position >= len(self.tokens):
            return True
        return False
    
    def get_position_info(self, token=None):
        """Get formatted position information for error messages"""
        if token is None:
            token = self.current_token()
        if hasattr(token, 'line') and token.line is not None:
            pos_info = f" at line {token.line}"
            if hasattr(token, 'column') and token.column is not None:
                pos_info += f", column {token.column}"
            return pos_info
        return ""
    def match(self, token_type, value=None):
        if not self.current_token():
            return False
        if self.current_token().type == token_type and (value is None or self.current_token().value == value):
            return True
        else:
            return False
    def expect(self, token_type, value=None):
        current = self.current_token()
        expected = value if value else token_type
        
        if current.type == token_type and (value is None or current.value == value):
            self.advance()
        else:
            # Build detailed error message with position info
            pos_info = self.get_position_info(current)
            
            if value:
                raise CompilerError(f"Expected '{value}'{pos_info} but found '{current.value}' ({current.type})", "ERROR", self.file_path)
            else:
                raise CompilerError(f"Expected {token_type}{pos_info} but found '{current.value}' ({current.type})", "ERROR", self.file_path)
    def parse_primary(self):
        if self.current_token().type == "INTEGER_LITERAL":
            token = self.current_token()
            literal = NumberLiteral(token.value, token.line, token.column)
            self.advance()
            return literal
        elif self.current_token().type == "FLOAT_LITERAL":
            token = self.current_token()
            literal = FloatLiteral(float(token.value), token.line, token.column)
            self.advance()
            return literal
        elif self.current_token().type == "STRING":
            token = self.current_token()
            literal = StringLiteral(token.value, token.line, token.column)
            self.advance()
            return literal
        elif self.current_token().type == "NAME":
            token = self.current_token()
            literal = Identifier(token.value, token.line, token.column)
            self.advance()
            
            # Check for array indexing: identifier[expression]
            if self.current_token().type == "SYMBOL" and self.current_token().value == "[":
                self.advance()  # consume '['
                index = self.parse_expression()
                self.expect("SYMBOL", "]")
                return IndexExpression(literal, index)
            
            return literal
        elif self.current_token().type == "KEYWORD" and self.current_token().value == "typeof":
            self.advance()
            # expect opening paren
            self.expect("SYMBOL", "(")
            node = self.parse_expression()
            self.expect("SYMBOL", ")")
            return TypeofExpression(node)
        elif self.match("KEYWORD", "int") or self.match("KEYWORD", "float") or self.match("KEYWORD", "string") or self.match("KEYWORD", "array") or self.match("KEYWORD", "bool") or self.match("KEYWORD", "void"):
            value = self.current_token().value
            self.advance()
            return TypeLiteral(value)  
        elif self.current_token().type == "SYMBOL":
            if self.current_token().value == "(":
                self.advance()
                node = self.parse_expression()
                self.expect("SYMBOL", ")")
                return node
            if self.current_token().value == "[":
                self.advance()
                elements = []
                if not self.match("SYMBOL", "]"):
                    element = self.parse_expression()
                    elements.append(element)
                    while self.match("SYMBOL", ","):
                        self.advance()
                        elements.append(self.parse_expression())
                self.expect("SYMBOL", "]")
                return ArrayLiteral(elements) 
        elif self.match("KEYWORD", "true"):
            self.advance()
            return BoolLiteral(True)
        elif self.match("KEYWORD", "false"):
            self.advance()
            return BoolLiteral(False)
    def parse_call_member(self):
        base = self.parse_primary()
        while self.match("SYMBOL", "(") or self.match("SYMBOL", "."):
            if self.match("SYMBOL", "("):
                self.advance()
                args = []
                if not self.match("SYMBOL", ")"):
                    args.append(self.parse_expression())
                    while self.match("SYMBOL", ","):
                        self.advance()
                        args.append(self.parse_expression())
                self.expect("SYMBOL", ")")
                base = CallExpression(base, args)
            elif self.match("SYMBOL", "."):
                self.advance()
                if self.current_token().type == "NAME" or self.current_token().type == "KEYWORD":
                    member_property = self.current_token().value
                    self.advance()
                else:
                    current = self.current_token()
                    pos_info = self.get_position_info(current)
                    raise CompilerError(f"Invalid member property: '{current.value}'{pos_info} - expected NAME or KEYWORD", "ERROR", self.file_path)
                base = MemberExpression(base, member_property)
                # Check if this is followed by function call parentheses
                if self.match("SYMBOL", "("):
                    # This is a method call like arr.length()
                    self.advance()
                    args = []
                    if not self.match("SYMBOL", ")"):
                        args.append(self.parse_expression())
                        while self.match("SYMBOL", ","):
                            self.advance()
                            args.append(self.parse_expression())
                    self.expect("SYMBOL", ")")
                    base = CallExpression(base, args)
        return base   
    def parse_multiplicative(self):
        left = self.parse_call_member()
        while self.current_token().value == "*" or self.current_token().value == "/":
            operator = self.current_token()
            self.advance()
            right = self.parse_call_member()
            expression = BinaryExpression(left, operator, right)
            left = expression
        return left
    def parse_additive(self):
        left = self.parse_multiplicative()
        while self.current_token().value == "+" or self.current_token().value == "-":
            operator = self.current_token()
            self.advance()
            right = self.parse_multiplicative()
            expression = BinaryExpression(left, operator, right)
            left = expression
        return left
    def parse_comparison(self):
        left = self.parse_additive()
        while self.current_token().value == "<" or self.current_token().value == ">" or self.current_token().value == ">=" or self.current_token().value == "<=":
            operator = self.current_token()
            self.advance()
            right = self.parse_additive()
            expression = BinaryExpression(left, operator, right)
            left = expression
        return left
    def parse_equality(self):
        left = self.parse_comparison()
        while self.current_token().value == "==" or self.current_token().value == "!=":
            operator = self.current_token()
            self.advance()
            right = self.parse_comparison()
            expression = BinaryExpression(left, operator, right)
            left = expression
        return left
    def parse_and(self):
        left = self.parse_equality()
        while self.current_token().value == "&&":
            operator = self.current_token()
            self.advance()
            right = self.parse_equality()
            expression = BinaryExpression(left, operator, right)
            left = expression
        return left
    def parse_or(self):
        left = self.parse_and()
        while self.current_token().value == "||":
            operator = self.current_token()
            self.advance()
            right = self.parse_and()
            expression = BinaryExpression(left, operator, right)
            left = expression
        return left
    def parse_expression(self):
        return self.parse_or()
    def parse_statement(self):
        if self.match("KEYWORD", "set"):
            return self.parse_set_statement()
        elif self.match("KEYWORD", "return"):
            return self.parse_return_statement()
        elif self.match("KEYWORD", "if"):
            return self.parse_if_statement()
        elif self.match("KEYWORD", "for"):
            return self.parse_for_statement()
        elif self.match("KEYWORD", "func"):
            return self.parse_dynamic_function()
        elif self.match("KEYWORD", "int") or self.match("KEYWORD", "float") or self.match("KEYWORD", "void") or self.match("KEYWORD", "array") or self.match("KEYWORD", "string") or self.match("KEYWORD", "bool"):
            return self.parse_declaration()
        elif self.match("KEYWORD", "from") or self.match("KEYWORD", "use"):
            return self.parse_import_statement()
        elif self.match("KEYWORD", "while"):
            return self.parse_while_statement()
        elif self.match("KEYWORD", "class"):
            # For now, skip class declarations (stub implementation)
            self.advance()  # consume 'class'
            class_name = self.current_token().value
            self.expect("NAME")
            # Skip to the end of class declaration
            while not self.match("SYMBOL", "}"):
                if self.is_at_end():
                    break
                self.advance()
            return None  # Skip class definitions for now
        else:
            return self.parse_expression_statement()
    def parse_expression_statement(self):
        expression = self.parse_expression()
        return ExpressionStatement(expression)
    def parse_set_statement(self):
        self.advance()
        var_token = self.current_token()
        var_name = var_token.value
        self.expect("NAME")
        self.expect("SYMBOL", "=")
        value = self.parse_expression()
        return SetStatement(var_name, value, var_token.line, var_token.column)
    def parse_return_statement(self):
        self.advance()
        if self.match("SYMBOL", "}") or self.is_at_end():
            value = None
        else:
            value = self.parse_expression()
        return ReturnStatement(value)
    def parse_block(self):
        self.expect("SYMBOL", "{")
        statements = []
        while not self.match("SYMBOL", "}"):
            statements.append(self.parse_statement())
        self.expect("SYMBOL", "}")
        return Block(statements)
    def parse_if_statement(self):
        self.advance()
        self.expect("SYMBOL", "(")
        condition = self.parse_expression()
        self.expect("SYMBOL", ")")
        body = self.parse_block()
        if self.match("KEYWORD", "else"):
            self.advance()
            else_body = self.parse_block()
        else:
            else_body = None
        return IfStatement(condition, body, else_body)
    def parse_for_statement(self):
        self.advance()
        self.expect("SYMBOL", "(")
        loop_var_name = self.current_token().value
        self.expect("NAME")
        self.expect("KEYWORD", "in")
        iterable = self.parse_expression()
        self.expect("SYMBOL", ")")
        body = self.parse_block()
        return ForInStatement(loop_var_name, iterable, body)
    def parse_while_statement(self):
        self.advance()
        self.expect("SYMBOL", "(")
        loop_condition = self.parse_expression()
        self.expect("SYMBOL", ")")
        body = self.parse_block()
        return WhileStatement(loop_condition, body)
    def parse_function_declaration(self):
        return_type = self.current_token().value
        self.advance()
        func_name = self.current_token().value
        self.expect("NAME")
        self.expect("SYMBOL", "(")
        params = []
        if not self.match("SYMBOL", ")"):
            param = self.parse_parameter()
            params.append(param)
            while self.match("SYMBOL", ","):
                self.advance()
                param = self.parse_parameter()
                params.append(param)
        self.expect("SYMBOL", ")")
        body = self.parse_block()
        return FunctionDeclaration(return_type, func_name, params, body)
    
    def parse_dynamic_function(self):
        self.advance()  # consume 'func'
        func_name = self.current_token().value
        self.expect("NAME")
        
        self.expect("SYMBOL", "(")
        params = []
        if not self.match("SYMBOL", ")"):
            # For dynamic functions, parameters have static type annotations
            param = self.parse_parameter()
            params.append(param)
            while self.match("SYMBOL", ","):
                self.advance()
                param = self.parse_parameter()
                params.append(param)
        self.expect("SYMBOL", ")")
        
        # Parse function body
        self.expect("SYMBOL", "{")
        statements = []
        while not self.match("SYMBOL", "}"):
            statements.append(self.parse_statement())
        self.expect("SYMBOL", "}")
        body = Block(statements)
        return DynamicFunctionDeclaration(func_name, params, body)
    
    def parse_parameter(self):
        param_type = self.current_token().value
        self.expect("KEYWORD")
        param_name = self.current_token().value
        self.expect("NAME")
        return Parameter(param_name, param_type)
    def parse_variable_declaration(self):
        var_type = self.current_token().value
        self.advance()
        var_name = self.current_token().value
        self.expect("NAME")
        if self.match("SYMBOL", "="):
            self.advance()
            value = self.parse_expression()
        else:
            value = None
        return VariableDeclaration(var_type, var_name, value)
    def parse_declaration(self):
        next_token = self.peek_token(2)
        if next_token:
            if next_token.value == "(" and next_token.type == "SYMBOL":
                return self.parse_function_declaration()
            else:
                return self.parse_variable_declaration()    
    def parse_import_statement(self):
        if self.match("KEYWORD", "from"):
            self.advance()
            module_path = self.parse_expression()
            self.expect("KEYWORD", "use")
            # Parse comma-separated symbols
            symbols = []
            symbol_name = self.current_token().value
            self.expect("NAME")
            symbols.append(symbol_name)
            while self.match("SYMBOL", ","):
                self.advance()
                symbol_name = self.current_token().value
                self.expect("NAME")
                symbols.append(symbol_name)
            return FromImportStatement(module_path, symbols)
        elif self.match("KEYWORD", "use"):
            self.advance()
            module_name = self.current_token().value
            self.expect("NAME")
            alias = None
            if self.match("KEYWORD", "as"):
                self.advance()
                alias = self.current_token().value
            return SimpleImportStatement(module_name, alias)
    def parse_program(self):
        statements = []
        while not self.is_at_end():
            statements.append(self.parse_statement())
        return Program(statements)