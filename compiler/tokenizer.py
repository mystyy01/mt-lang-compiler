KEYWORDS = ["from", "use", "as", "int", "float", "void", "array", "string", "bool", "if", "else", "for", "in", "set", "return", "typeof", "while", "bool", "true", "false", "func", "class", "new", "this", "static", "virtual", "arg", "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "malloc", "free"]
SINGLE_CHAR_SYMBOLS = ["(", ")", "[", "]", "{", "}", ",", ".", ":", "!", "+", "-", "*", "/", "=", ">", "<"]
DOUBLE_CHAR_SYMBOLS = ["==", "!=", "+=", ">=", "<=", "&&", "||"]
QUOTES = ['"', "'"]
WHITESPACE = [" ", "\t", "\n"] # space, tab, newline (i think thats a tab char i may be wrong)
class Token:
    def __init__(self, _type, value, line=None, column=None):
        self.type = _type # we use type to stop conflicts with pythons built in type() function
        self.value = value
        self.line = line
        self.column = column
    def __repr__(self):
        return f"{self.type}: {self.value}"
class CompilerError(Exception):
    def __init__(self, message, severity, file_path=None):
        file_info = f" in {file_path}" if file_path and file_path != "unknown" else ""
        super().__init__(message + f"{file_info} (Severity: {severity})")
        # change text colour to yellow for warnings and stop compiling and turn text red for error

class Tokenizer:
    def __init__(self, source: str, file_path=None):
        self.source = source
        self.position = 0
        self.line = 1
        self.column = 1
        self.file_path = file_path or "unknown"
    def current_char(self):
        return self.source[self.position]
    def advance(self, offset=1):
        for _ in range(offset):
            if self.position < len(self.source):
                if self.source[self.position] == '\n':
                    self.line += 1
                    self.column = 1
                else:
                    self.column += 1
            self.position += 1
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
            # C-style comment: //
            self.advance()  # consume first /
            self.advance()  # consume second /
            while not self.is_at_end() and self.current_char() != "\n":
                self.advance()
    def read_word(self):
        word = ""
        start_line = self.line
        start_column = self.column
        while not self.is_at_end() and (self.current_char().isalnum() or self.current_char() == "_"):
            word += self.current_char()
            self.advance()
        if word in KEYWORDS:
            return Token("KEYWORD", word, start_line, start_column)
        else:
            return Token("NAME", word, start_line, start_column)
    def read_number(self):
        number = ""
        has_decimal = False
        has_exponent = False
        start_line = self.line
        start_column = self.column
        
        # Read digits before decimal point
        while not self.is_at_end() and self.current_char().isdigit():
            number += self.current_char()
            self.advance()
        
        # Handle decimal point
        if not self.is_at_end() and self.current_char() == '.':
            has_decimal = True
            number += '.'
            self.advance()
            
            # Read digits after decimal point (at least one digit required)
            if not self.is_at_end() and self.current_char().isdigit():
                while not self.is_at_end() and self.current_char().isdigit():
                    number += self.current_char()
                    self.advance()
            else:
                raise CompilerError(f"Invalid float literal at line {start_line}: expected digits after decimal point", "ERROR", self.file_path)
        
        # Handle scientific notation
        if not self.is_at_end() and self.current_char().lower() == 'e':
            has_exponent = True
            number += 'e'
            self.advance()
            
            # Handle exponent sign
            if not self.is_at_end() and self.current_char() in '+-':
                number += self.current_char()
                self.advance()
                
            # Read exponent digits (at least one digit required)
            if not self.is_at_end() and self.current_char().isdigit():
                while not self.is_at_end() and self.current_char().isdigit():
                    number += self.current_char()
                    self.advance()
            else:
                raise CompilerError(f"Invalid float literal at line {start_line}: expected digits in exponent", "ERROR", self.file_path)
        
        # Determine token type based on whether we saw a decimal point or exponent
        is_float = has_decimal or has_exponent
        token_type = "FLOAT_LITERAL" if is_float else "INTEGER_LITERAL"
        return Token(token_type, number, start_line, start_column)
    def read_string(self):
        string = ""
        # double check the current char is a quote
        if not self.current_char() in QUOTES:
            return None
        start_line = self.line
        start_column = self.column
        self.advance()
        while not self.is_at_end() and self.current_char() not in QUOTES:
            if self.current_char() == '\\' and not self.is_at_end():
                # Handle escape sequences
                self.advance()
                if self.is_at_end():
                    break
                escape_char = self.current_char()
                if escape_char == 'n':
                    string += '\n'
                elif escape_char == 't':
                    string += '\t'
                elif escape_char == 'r':
                    string += '\r'
                elif escape_char == '\\':
                    string += '\\'
                elif escape_char == '"':
                    string += '"'
                elif escape_char == "'":
                    string += "'"
                elif escape_char == '0':
                    string += '\0'
                else:
                    # Unknown escape, keep as-is
                    string += '\\' + escape_char
            else:
                string += self.current_char()
            self.advance()
        self.advance()
        return Token("STRING", string, start_line, start_column)
    def read_symbol(self):
        if not self.is_at_end():
            # Check for // comment start first
            if self.current_char() == "/" and self.peek_next() == "/":
                # Skip the entire comment
                self.advance()  # consume first /
                self.advance()  # consume second /
                while not self.is_at_end() and self.current_char() != "\n":
                    self.advance()
                # Return None to indicate no token was produced (comment skipped)
                return None
            elif self.peek_next() and (self.current_char() + self.peek_next()) in DOUBLE_CHAR_SYMBOLS:
                token =  Token("SYMBOL", (self.current_char() + self.peek_next()), self.line, self.column)
                self.advance(2)
                return token
            elif self.current_char() in SINGLE_CHAR_SYMBOLS:
                token = Token("SYMBOL", self.current_char(), self.line, self.column)
                self.advance()
                return token
            else:
                raise CompilerError(f"Unknown symbol '{self.current_char()}' at line {self.line}, column {self.column}", "ERROR", self.file_path)
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
            elif self.current_char() == '-':
                # Check if this is a negative number (unary minus)
                # Look ahead to see if next character starts a number
                next_char = self.peek_next() if not self.is_at_end() else None
                
                # If next character is a digit, this is a negative number
                if next_char and next_char.isdigit():
                    self.advance()  # consume the minus
                    number = "-"
                    start_line = self.line
                    start_column = self.column
                    
                    # Read the actual number
                    num_token = self.read_number()
                    number += num_token.value
                    
                    # Determine if it's float or int based on the number token
                    token_type = "FLOAT_LITERAL" if num_token.type == "FLOAT_LITERAL" else "INTEGER_LITERAL"
                    tokens.append(Token(token_type, number, start_line, start_column))
                else:
                    # This is a binary minus operator
                    tokens.append(self.read_symbol())
            elif self.current_char() in SINGLE_CHAR_SYMBOLS or (self.peek_next() and (self.current_char() + self.peek_next()) in DOUBLE_CHAR_SYMBOLS) or (self.current_char() == "/" and self.peek_next() == "/"):
                symbol_token = self.read_symbol()
                if symbol_token is not None:  # Skip None tokens (comments)
                    tokens.append(symbol_token)
            elif self.current_char().isalpha() or self.current_char() == "_":
                tokens.append(self.read_word())
            elif self.current_char().isdigit():
                tokens.append(self.read_number())
            else:
                # Debug: show what character is causing issue
                char = self.current_char() if not self.is_at_end() else "EOF"
                raise CompilerError(f"Tokenizer failed at char {self.position}: '{char}' (line {self.line}, col {self.column})", "ERROR", self.file_path)
        return tokens