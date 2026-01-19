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

            return literal
        elif self.current_token().type == "KEYWORD" and self.current_token().value == "null":
            self.advance()
            return NullLiteral()
        elif self.current_token().type == "KEYWORD" and self.current_token().value == "typeof":
            self.advance()
            # expect opening paren
            self.expect("SYMBOL", "(")
            node = self.parse_expression()
            self.expect("SYMBOL", ")")
            return TypeofExpression(node)
        elif self.current_token().type == "KEYWORD" and self.current_token().value == "hasattr":
            self.advance()
            self.expect("SYMBOL", "(")
            obj = self.parse_expression()
            self.expect("SYMBOL", ",")
            # attribute name must be a string literal
            if self.current_token().type != "STRING":
                raise SyntaxError(f"hasattr requires a string literal for attribute name{self.get_position_info()}")
            attr_name = self.current_token().value
            self.advance()
            self.expect("SYMBOL", ")")
            return HasattrExpression(obj, attr_name)
        elif self.current_token().type == "KEYWORD" and self.current_token().value == "classof":
            self.advance()
            self.expect("SYMBOL", "(")
            node = self.parse_expression()
            self.expect("SYMBOL", ")")
            return ClassofExpression(node)
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
        while self.match("SYMBOL", "(") or self.match("SYMBOL", ".") or self.match("SYMBOL", "["):
            if self.match("SYMBOL", "("):
                self.advance()
                args = []
                if not self.match("SYMBOL", ")"):
                    args.append(self.parse_expression())
                    while self.match("SYMBOL", ","):
                        self.advance()
                        args.append(self.parse_expression())
                self.expect("SYMBOL", ")")
                # Get position from base (the callee)
                line = getattr(base, 'line', None)
                column = getattr(base, 'column', None)
                base = CallExpression(base, args, line=line, column=column)
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
                    # Get position from base (MemberExpression)
                    line = getattr(base, 'line', None)
                    column = getattr(base, 'column', None)
                    base = CallExpression(base, args, line=line, column=column)
            elif self.match("SYMBOL", "["):
                self.advance()
                index = self.parse_expression()
                self.expect("SYMBOL", "]")
                base = IndexExpression(base, index)
        return base
    
    def parse_function_call(self, callee):
        """Parse a function call with the given callee"""
        print(f"DEBUG: Parsing function call to {callee.name}")
        self.advance()  # consume '('
        args = []
        if not self.match("SYMBOL", ")"):
            args.append(self.parse_expression())
            while self.match("SYMBOL", ","):
                self.advance()
                args.append(self.parse_expression())
        self.expect("SYMBOL", ")")
        print(f"DEBUG: Created CallExpression with {len(args)} args for {callee.name}")
        # Get position from callee
        line = getattr(callee, 'line', None)
        column = getattr(callee, 'column', None)
        return CallExpression(callee, args, line=line, column=column)
    
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
    def parse_in(self):
        left = self.parse_equality()
        while self.current_token().type == "KEYWORD" and self.current_token().value == "in":
            operator = self.current_token()
            self.advance()
            right = self.parse_equality()
            expression = InExpression(left, right)
            left = expression
        return left
    def parse_and(self):
        left = self.parse_in()
        while self.current_token().value == "&&":
            operator = self.current_token()
            self.advance()
            right = self.parse_in()
            expression = BinaryExpression(left, operator, right)
            left = expression
        return left
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
              self.match("KEYWORD", "array") or self.match("KEYWORD", "string") or self.match("KEYWORD", "bool")):
            # Check if this is a builtin function call like int(x) or a declaration like int foo = ...
            # Look ahead: if next token is (, it's a builtin call; if next is NAME, it could be declaration
            if self.peek_token() and self.peek_token().type == "SYMBOL" and self.peek_token().value == "(":
                # Builtin function call - parse as expression
                return self.parse_expression_statement()
            elif self.current_token().type == "NAME" or (self.peek_token() and self.peek_token().type == "NAME"):
                return self.parse_declaration()
            elif self.peek_token() and self.peek_token().type == "SYMBOL" and self.peek_token().value == "<":
                # Handle array<Type> syntax - this is a typed array declaration
                return self.parse_declaration()
            else:
                # Fallback: parse as expression
                return self.parse_expression_statement()
        elif (self.current_token().type == "NAME" and self.peek_token() and self.peek_token().type == "NAME"):
            return self.parse_declaration()
        elif self.match("KEYWORD", "from") or self.match("KEYWORD", "use"):
            return self.parse_import_statement()
        elif self.match("KEYWORD", "while"):
            return self.parse_while_statement()
        elif self.match("KEYWORD", "class"):
            return self.parse_class_declaration()
        elif self.match("KEYWORD", "try"):
            return self.parse_try_statement()
        elif self.match("KEYWORD", "throw"):
            return self.parse_throw_statement()
        elif self.match("KEYWORD", "break"):
            return self.parse_break_statement()
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
            if self.is_at_end():
                raise CompilerError(f"Unterminated block - expected '}}' but reached end of file", "ERROR", self.file_path)
            start_pos = self.position
            statements.append(self.parse_statement())
            # Check if parser made progress - if not, we're stuck on an unrecognized token
            if self.position == start_pos:
                token = self.current_token()
                raise CompilerError(f"Unexpected token '{token.value}' at line {token.line}, column {token.column}", "ERROR", self.file_path)
        self.expect("SYMBOL", "}")
        return Block(statements)
    def parse_if_statement(self):
        self.advance()  # Move past 'if' or 'elif'
        self.expect("SYMBOL", "(")
        condition = self.parse_expression()
        self.expect("SYMBOL", ")")
        body = self.parse_block()
        if self.match("KEYWORD", "elif"):
            # elif is syntactic sugar for else { if ... }
            # Recursively parse the elif as a nested if statement
            nested_if = self.parse_if_statement()
            else_body = Block([nested_if])
        elif self.match("KEYWORD", "else"):
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
    def parse_try_statement(self):
        """Parse try/catch/except statement"""
        start_line = self.current_token().line
        start_column = self.current_token().column
        self.expect("KEYWORD", "try")
        
        # Parse try block
        try_block = self.parse_block()
        
        # Parse one or more catch/except blocks
        catch_blocks = []
        while self.match("KEYWORD", "catch") or self.match("KEYWORD", "except"):
            catch_block = self.parse_catch_block()
            catch_blocks.append(catch_block)
        
        if not catch_blocks:
            raise CompilerError(f"try statement must have at least one catch block", "ERROR", self.file_path)
        
        return TryStatement(try_block, catch_blocks, start_line, start_column)
    
    def parse_catch_block(self):
        """Parse a catch or except block"""
        start_line = self.current_token().line
        start_column = self.current_token().column
        
        # Handle both 'catch' and 'except' keywords
        if self.match("KEYWORD", "catch"):
            self.advance()
        elif self.match("KEYWORD", "except"):
            self.advance()
        
        exception_type = None
        identifier = None
        
        # Check if we have an exception type (next token is NAME/KEYWORD)
        if (self.current_token().type == "NAME" or self.current_token().type == "KEYWORD") and self.current_token().value not in ["{"]:
            exception_type = self.current_token().value
            self.advance()
        
        # Parse optional (var) or (Type var) binding
        if self.match("SYMBOL", "("):
            self.advance()
            # Variable name
            if self.current_token().type == "NAME":
                identifier = self.current_token().value
                self.advance()
            else:
                raise CompilerError(f"Expected exception variable name in catch block", "ERROR", self.file_path)
            self.expect("SYMBOL", ")")
        
        # Parse catch body
        body = self.parse_block()
        
        return CatchBlock(exception_type, identifier, body, start_line, start_column)
    
    def parse_throw_statement(self):
        """Parse throw statement"""
        start_line = self.current_token().line
        start_column = self.current_token().column
        self.expect("KEYWORD", "throw")

        # Check if there's an expression to throw or if it's a standalone throw
        if self.match("SYMBOL", "}") or self.is_at_end() or self.match("KEYWORD", "catch") or self.match("KEYWORD", "else"):
            # Standalone throw - no expression
            expression = None
        else:
            # Parse the expression to throw
            expression = self.parse_expression()

        return ThrowStatement(expression, start_line, start_column)
    
    def parse_break_statement(self):
        """Parse break statement"""
        start_line = self.current_token().line
        start_column = self.current_token().column
        self.expect("KEYWORD", "break")
        return BreakStatement(start_line, start_column)
    
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
    
    def parse_type_with_element(self):
        """Parse type that may include element type: array<int>, array<Token>"""
        base_type = self.current_token().value
        self.advance()

        element_type = None
        if base_type == "array" and self.match("SYMBOL", "<"):
            self.advance()  # consume '<'
            if self.current_token().type == "KEYWORD":
                element_type = self.current_token().value
            elif self.current_token().type == "NAME":
                element_type = self.current_token().value
            else:
                current = self.current_token()
                pos_info = self.get_position_info(current)
                raise CompilerError(f"Expected type name after '<'{pos_info} but found '{current.value}'", "ERROR", self.file_path)
            self.advance()
            self.expect("SYMBOL", ">")

        return base_type, element_type

    def parse_parameter(self):
        base_type, element_type = self.parse_type_with_element()
        param_name = self.current_token().value
        self.expect("NAME")
        default_value = None
        if self.match("SYMBOL", "="):
            self.advance()
            default_value = self.parse_expression()
        return Parameter(param_name, base_type, default_value, element_type)
    def parse_variable_declaration(self):
        base_type, element_type = self.parse_type_with_element()
        var_name = self.current_token().value
        self.expect("NAME")
        if self.match("SYMBOL", "="):
            self.advance()
            value = self.parse_expression()
        else:
            value = None
        return VariableDeclaration(base_type, var_name, value, element_type)
    def parse_declaration(self):
        # Handle array<Type> syntax - need to look past <Type> to find the name
        offset = 1  # Default: type name
        if self.current_token().value == "array" and self.peek_token(1) and self.peek_token(1).value == "<":
            # array<Type> name - skip past < Type >
            offset = 4  # array < Type > name

        next_token = self.peek_token(offset + 1)
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
            # Check for wildcard import
            if self.match("SYMBOL", "*"):
                self.advance()
                # Wildcard import - cannot use with libc
                if isinstance(module_path, Identifier) and module_path.name == "libc":
                    self.add_error("Wildcard import not allowed for libc module")
                    return FromImportStatement(module_path, [], is_wildcard=True)
                return FromImportStatement(module_path, None, is_wildcard=True)
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
            # Check if this is a libc import
            if isinstance(module_path, Identifier) and module_path.name == "libc":
                return LibcImportStatement(symbols)
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
        
        # Check for inheritance
        inherits_from = None
        if self.match("NAME", "inherits") or self.match("NAME", "extends"):
            self.advance()
            inherits_from = self.current_token().value
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
                    # Handle array<Type> syntax
                    if token.value == "array" and peek_pos < len(self.tokens) and self.tokens[peek_pos].type == "SYMBOL" and self.tokens[peek_pos].value == "<":
                        peek_pos += 1  # skip <
                        if peek_pos < len(self.tokens):
                            peek_pos += 1  # skip element type
                        if peek_pos < len(self.tokens) and self.tokens[peek_pos].type == "SYMBOL" and self.tokens[peek_pos].value == ">":
                            peek_pos += 1  # skip >
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
        return ClassDeclaration(class_name, fields, methods, inherits_from=inherits_from)

    def parse_field_declaration(self):
        # Check for arg keyword (only valid in class context)
        is_constructor_arg = False
        if self.match("KEYWORD", "arg"):
            self.advance()
            is_constructor_arg = True

        field_type, element_type = self.parse_type_with_element()
        field_name = self.current_token().value
        self.expect("NAME")

        # Check for initializer
        initializer = None
        if self.match("SYMBOL", "="):
            self.advance()
            initializer = self.parse_expression()

        return FieldDeclaration(field_name, field_type, initializer=initializer, is_constructor_arg=is_constructor_arg, element_type=element_type)

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