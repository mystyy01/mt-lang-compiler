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

    def peek_match(self, token_type, value=None, offset=0):
        """Check if token at current position + offset matches without consuming"""
        pos = self.position + offset
        if pos >= len(self.tokens):
            return False
        token = self.tokens[pos]
        return token.type == token_type and (value is None or token.value == value)
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
        elif self.current_token().type == "NAME" or (self.current_token().type == "KEYWORD" and self.current_token().value in ["fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "malloc", "free", "int", "str", "float", "read"]):
            token = self.current_token()
            literal = Identifier(token.value, token.line, token.column)
            self.advance()

            # Check for function call: identifier(expression)
            if self.current_token().type == "SYMBOL" and self.current_token().value == "(":
                # This is a function call, delegate to call parsing
                return self.parse_function_call(literal)
            
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
        elif self.match("KEYWORD", "this"):
            self.advance()
            return ThisExpression()
        elif self.match("KEYWORD", "new"):
            self.advance()
            class_name = self.current_token().value
            self.expect("NAME")
            self.expect("SYMBOL", "(")
            args = []
            if not self.match("SYMBOL", ")"):
                args.append(self.parse_expression())
                while self.match("SYMBOL", ","):
                    self.advance()
                    args.append(self.parse_expression())
            self.expect("SYMBOL", ")")
            return NewExpression(class_name, args)
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
        else:
            current = self.current_token()
            pos_info = self.get_position_info(current)
            raise CompilerError(f"Unexpected token {current.type} '{current.value}'{pos_info}", "ERROR", self.file_path)
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
    
    def parse_function_call(self, callee):
        """Parse a function call with the given callee"""
        self.advance()  # consume '('
        args = []
        if not self.match("SYMBOL", ")"):
            args.append(self.parse_expression())
            while self.match("SYMBOL", ","):
                self.advance()
                args.append(self.parse_expression())
        self.expect("SYMBOL", ")")
        return CallExpression(callee, args)
    
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
        elif (self.match("KEYWORD", "int") or self.match("KEYWORD", "float") or self.match("KEYWORD", "void") or
              self.match("KEYWORD", "array") or self.match("KEYWORD", "string") or self.match("KEYWORD", "bool") or
              (self.current_token().type == "NAME" and self.peek_token() and self.peek_token().type == "NAME")):
            return self.parse_declaration()
        elif self.match("KEYWORD", "from") or self.match("KEYWORD", "use"):
            return self.parse_import_statement()
        elif self.match("KEYWORD", "while"):
            return self.parse_while_statement()
        elif self.match("KEYWORD", "class"):
            return self.parse_class_declaration()
        else:
            return self.parse_expression_statement()
    def parse_expression_statement(self):
        expression = self.parse_expression()
        return ExpressionStatement(expression)
    def parse_set_statement(self):
        self.expect("KEYWORD", "set")
        target = self.parse_call_member()
        self.expect("SYMBOL", "=")
        value = self.parse_expression()
        return SetStatement(target, value)
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
    def parse_class_declaration(self):
        self.advance()  # consume 'class'
        class_name = self.current_token().value
        self.expect("NAME")
        self.expect("SYMBOL", "{")

        fields = []
        methods = []

        while not self.match("SYMBOL", "}"):
            if self.is_at_end():
                raise CompilerError("Unterminated class declaration", "ERROR", self.file_path)

            # Look ahead without consuming to determine if this is a field or method
            saved_pos = self.position

            # Check for static/virtual modifiers
            has_static = self.peek_match("KEYWORD", "static")
            has_virtual = self.peek_match("KEYWORD", "virtual") if not has_static else False

            # Skip modifiers in peek
            peek_pos = self.position
            if has_static:
                peek_pos += 1
            elif has_virtual:
                peek_pos += 1

            # Check if next is a type keyword, name (user-defined type), or func
            type_match = False
            is_func = False
            has_arg = False
            if peek_pos < len(self.tokens):
                token = self.tokens[peek_pos]
                if token.type == "KEYWORD" and token.value == "arg":
                    has_arg = True
                    peek_pos += 1  # skip 'arg'
                    token = self.tokens[peek_pos] if peek_pos < len(self.tokens) else None

                if token and token.type == "KEYWORD" and token.value in ["int", "float", "void", "string", "bool", "array"]:
                    type_match = True
                    peek_pos += 1  # skip type
                elif token and token.type == "NAME":
                    # User-defined type (class name)
                    type_match = True
                    peek_pos += 1  # skip type
                elif token and token.type == "KEYWORD" and token.value == "func":
                    is_func = True
                    peek_pos += 1  # skip func

                if type_match or is_func:
                    # Check if next is name
                    if peek_pos < len(self.tokens) and self.tokens[peek_pos].type == "NAME":
                        peek_pos += 1  # skip name

                        # Check if next is (
                        if peek_pos < len(self.tokens) and self.tokens[peek_pos].type == "SYMBOL" and self.tokens[peek_pos].value == "(":
                            # It's a method
                            method = self.parse_method_declaration()
                            methods.append(method)
                        else:
                            # It's a field
                            field = self.parse_field_declaration()
                            fields.append(field)
                        continue

            # If we get here, it didn't match the expected pattern
            self.position = saved_pos  # reset position
            current = self.current_token()
            pos_info = self.get_position_info(current)
            raise CompilerError(f"Expected field or method declaration in class{pos_info}", "ERROR", self.file_path)

        self.expect("SYMBOL", "}")
        return ClassDeclaration(class_name, fields, methods)

    def parse_field_declaration(self):
        # Check for arg keyword (only valid in class context)
        is_constructor_arg = False
        if self.match("KEYWORD", "arg"):
            self.advance()
            is_constructor_arg = True

        field_type = self.current_token().value
        self.advance()
        field_name = self.current_token().value
        self.expect("NAME")

        # Check for initializer
        initializer = None
        if self.match("SYMBOL", "="):
            self.advance()
            initializer = self.parse_expression()

        return FieldDeclaration(field_name, field_type, initializer=initializer, is_constructor_arg=is_constructor_arg)

    def parse_method_declaration(self):
        is_static = False
        is_virtual = False

        # Check for static/virtual modifiers
        if self.match("KEYWORD", "static"):
            self.advance()
            is_static = True
        elif self.match("KEYWORD", "virtual"):
            self.advance()
            is_virtual = True

        # Parse return type or func keyword
        if self.match("KEYWORD", "func"):
            # Dynamic function method - no explicit return type
            self.advance()
            return_type = None
        else:
            # Regular method with return type (can be built-in or user-defined)
            return_type = self.current_token().value
            # Accept either KEYWORD or NAME (for user-defined types)
            if self.current_token().type == "KEYWORD" or self.current_token().type == "NAME":
                self.advance()  # consume return type
            else:
                raise CompilerError(f"Expected return type but found '{self.current_token().value}' ({self.current_token().type})", "ERROR", self.file_path)

        # Parse method name
        method_name = self.current_token().value
        self.expect("NAME")

        # Parse parameters
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

        # Parse method body
        body = self.parse_block()

        return MethodDeclaration(method_name, params, return_type, body, is_virtual, is_static)

    def parse_program(self):
        statements = []
        while not self.is_at_end():
            statements.append(self.parse_statement())
        return Program(statements)