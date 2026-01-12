KEYWORDS = ["from", "use", "as", "int", "void", "array", "string", "bool", "if", "else", "for", "in", "set", "return", "typeof"]
SINGLE_CHAR_SYMBOLS = ["(", ")", "[", "]", "{", "}", ",", ".", "+", "-", "*", "/", "="]
DOUBLE_CHAR_SYMBOLS = ["==", "!=", "+=", ">=", "<=", "&&", "||"]
QUOTES = ['"', "'"]
WHITESPACE = [" ", "\t", "\n"] # space, tab, newline (i think thats a tab char i may be wrong)
class Token:
    def __init__(self, _type, value):
        self.type = _type # we use type to stop conflicts with pythons built in type() function
        self.value = value
    def __repr__(self):
        return f"{self.type}: {self.value}"
class CompilerError(Exception):
    def __init__(self, message, severity):
        super().__init__(message + f" (Severity: {severity})")
        # change text colour to yellow for warnings and stop compiling and turn text red for error

class Tokenizer:
    def __init__(self, source: str):
        self.source = source
        self.position = 0
    def current_char(self):
        return self.source[self.position]
    def advance(self, offset=1):
        self.position += offset
    def is_at_end(self):
        if self.position >= len(self.source):
            return True
        return False
    def peek_next(self):
        if self.position + 1 < len(self.source):
            return self.source[self.position+1]
        return None
    def skip_whitespace(self):
        while not self.is_at_end() and self.current_char() in WHITESPACE:
            self.advance()
    def skip_comment(self):
        if not self.is_at_end() and self.current_char() == "/" and self.peek_next() == "/":
            while not self.is_at_end() and self.current_char() != "\n":
                self.advance()
    def read_word(self):
        word = ""
        while not self.is_at_end() and (self.current_char().isalnum() or self.current_char() == "_"):
            word += self.current_char()
            self.advance()
        if word in KEYWORDS:
            return Token("KEYWORD", word)
        else:
            return Token("NAME", word)
    def read_number(self):
        number = ""
        while not self.is_at_end() and self.current_char().isdigit():
            number += self.current_char()
            self.advance()
        return Token("NUMBER", number)
    def read_string(self):
        string = ""
        # double check the current char is a quote
        if not self.current_char() in QUOTES:
            return None
        self.advance()
        while not self.is_at_end() and self.current_char() not in QUOTES:
            string += self.current_char()
            self.advance()
        self.advance()
        return Token("STRING", string)
    def read_symbol(self):
        if not self.is_at_end():
            if self.peek_next() and (self.current_char() + self.peek_next()) in DOUBLE_CHAR_SYMBOLS:
                token =  Token("SYMBOL", (self.current_char() + self.peek_next()))
                self.advance(2)
                return token
            elif self.current_char() in SINGLE_CHAR_SYMBOLS:
                token = Token("SYMBOL", self.current_char())
                self.advance()
                return token
            else:
                raise CompilerError("Unknown symbol in source code.", "ERROR")
    def tokenize(self):
        tokens = []
        while not self.is_at_end():
            self.skip_whitespace()
            self.skip_comment()
            self.skip_whitespace()
            if self.is_at_end():
                break
            if self.current_char() in QUOTES:
                tokens.append(self.read_string())
            elif self.current_char() in SINGLE_CHAR_SYMBOLS or (self.peek_next() and (self.current_char() + self.peek_next()) in DOUBLE_CHAR_SYMBOLS):
                tokens.append(self.read_symbol())
            elif self.current_char().isalpha() or self.current_char() == "_":
                tokens.append(self.read_word())
            elif self.current_char().isdigit():
                tokens.append(self.read_number())
            else:
                raise CompilerError(f"Tokenizer failed at char: {self.position}", "ERROR")
        return tokens