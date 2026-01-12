from tokenizer import Token, CompilerError
from ast_nodes import *
class Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.position = 0
    def current_token(self):
        return self.tokens[self.position]
    def advance(self, offset = 1):
        self.position += offset
    def peek_token(self):
        if self.position + 1 < len(self.tokens):
            return self.tokens[self.position+1]
        return None
    def is_at_end(self):
        if self.position >= len(self.tokens):
            return True
        return False
    def match(self, token_type, value=None):
        if self.current_token().type == token_type and (value is None or self.current_token().value == value):
            return True
        else:
            return False
    def expect(self, token_type, value=None):
        if self.current_token().type == token_type and (value is None or self.current_token().value == value):
            self.advance()
        else:
            raise CompilerError("Unexpected token", "ERROR")
    def parse_primary(self):
        if self.current_token().type == "NUMBER":
            literal = NumberLiteral(self.current_token().value)
            self.advance()
            return literal
        elif self.current_token().type == "STRING":
            literal = StringLiteral(self.current_token().value)
            self.advance()
            return literal
        elif self.current_token().type == "NAME":
            literal = Identifier(self.current_token().value)
            self.advance()
            return literal
        elif self.current_token().type == "KEYWORD" and self.current_token().value == "typeof":
            self.advance()
            # expect opening paren
            self.expect("SYMBOL", "(")
            node = self.parse_primary()
            self.expect("SYMBOL", ")")
            return TypeofExpression(node)
        elif self.current_token().type == "SYMBOL":
            if self.current_token().value == "(":
                self.advance()
                node = self.parse_primary()
                self.expect("SYMBOL", ")")
                return node
            if self.current_token().value == "[":
                self.advance()
                elements = []
                if not self.match("SYMBOL", "]"):
                    element = self.parse_primary()
                    elements.append(element)
                    while self.match("SYMBOL", ","):
                        self.advance()
                        elements.append(self.parse_primary())
                self.expect("SYMBOL", "]")
                return ArrayLiteral(elements)   
    def parse_call_member(self):
        base = self.parse_primary()
        while self.match("SYMBOL", "(") or self.match("SYMBOL", "."):
            if self.match("SYMBOL", "("):
                self.advance()
                args = []
                if not self.match("SYMBOL", ")"):
                    args.append(self.parse_primary())
                    while self.match("SYMBOL", ","):
                        self.advance()
                        args.append(self.parse_primary())
                self.expect("SYMBOL", ")")
                base = CallExpression(base, args)
            elif self.match("SYMBOL", "."):
                self.advance()
                member_property = self.current_token().value
                self.expect("NAME")
                base = MemberExpression(base, member_property)  
        return base   