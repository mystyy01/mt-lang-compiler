# AGENTS.md - mt-lang Compiler Development Guide

This document provides essential information for AI agents working on the mt-lang compiler codebase.

## Build, Lint, and Test Commands

### Building and Running
- **Activate virtual environment**: `source venv/bin/activate`
- **Compile a .mtc source file**: `python compiler.py source_file.mtc output_executable`
- **Full compilation workflow**:
  ```bash
  source venv/bin/activate
  python compiler.py source.mtc executable_name
  ./executable_name  # Run the compiled binary
  ```

### Testing
- **Run all tests**: There is no automated test runner. Tests are run manually by compiling and executing .mtc files in the `tests/` directory.
- **Run a single test**:
  ```bash
  source venv/bin/activate
  python compiler.py tests/test_file.mtc test_executable
  ./test_executable
  ```
- **Test file locations**: Test files are in `tests/*.mtc`, compiled executables are in `tests/` directory
- **Expected output**: Many tests have corresponding `_output` files showing expected results

### Linting and Code Quality
- **No configured linters**: The codebase does not use flake8, pylint, or other Python linting tools
- **No formatters**: No black, autopep8, or other code formatters are configured
- **Manual code review**: Code quality is maintained through manual review

## Language Syntax

mt-lang is a statically-typed programming language that compiles to LLVM IR. The language supports object-oriented programming with classes, functions, and modules.

### Lexical Structure

- **Keywords**: `from`, `use`, `as`, `int`, `float`, `void`, `array`, `string`, `bool`, `if`, `else`, `for`, `in`, `set`, `return`, `typeof`, `while`, `bool`, `true`, `false`, `func`, `class`, `new`, `this`
- **Operators**: `+`, `-`, `*`, `/`, `=`, `>`, `<`, `>=`, `<=`, `==`, `!=`, `+=`, `&&`, `||`
- **Symbols**: `(`, `)`, `[`, `]`, `{`, `}`, `,`, `.`
- **Literals**: integers, floats (with decimal/exponent), strings (double/single quotes), booleans (`true`/`false`), arrays `[...]`
- **Comments**: C-style single-line comments with `//`

### Grammar

```
program ::= statement*

statement ::= variable_declaration
           | function_declaration
           | dynamic_function_declaration
           | class_declaration
           | import_statement
           | set_statement
           | return_statement
           | if_statement
           | while_statement
           | for_statement
           | expression_statement

variable_declaration ::= type identifier ("=" expression)?

function_declaration ::= type identifier "(" parameters? ")" block

dynamic_function_declaration ::= "func" identifier "(" parameters? ")" block

parameters ::= parameter ("," parameter)*

parameter ::= type identifier

class_declaration ::= "class" identifier "{" (field_declaration | method_declaration)* "}"

field_declaration ::= ("arg")? type identifier ("=" expression)?

method_declaration ::= ("static" | "virtual")? (type | "func") identifier "(" parameters? ")" block

import_statement ::= "from" expression "use" identifier ("," identifier)*
                   | "use" identifier ("as" identifier)?

set_statement ::= "set" identifier "=" expression

return_statement ::= "return" expression?

if_statement ::= "if" "(" expression ")" block ("else" block)?

while_statement ::= "while" "(" expression ")" block

for_statement ::= "for" "(" identifier "in" expression ")" block

expression_statement ::= expression

expression ::= or_expression

or_expression ::= and_expression ("||" and_expression)*

and_expression ::= equality_expression ("&&" equality_expression)*

equality_expression ::= comparison_expression (("==" | "!=") comparison_expression)*

comparison_expression ::= additive_expression (("<" | ">" | "<=" | ">=") additive_expression)*

additive_expression ::= multiplicative_expression (("+" | "-") multiplicative_expression)*

multiplicative_expression ::= call_member_expression (("*" | "/") call_member_expression)*

call_member_expression ::= primary_expression (("." identifier | "(" arguments? ")"))*

primary_expression ::= integer_literal
                     | float_literal
                     | string_literal
                     | boolean_literal
                     | identifier ("[" expression "]")?
                     | "typeof" "(" expression ")"
                     | type_literal
                     | "(" expression ")"
                     | "[" arguments? "]"
                     | new_expression

arguments ::= expression ("," expression)*

new_expression ::= "new" identifier "(" arguments? ")"

type ::= "int" | "float" | "string" | "bool" | "void" | "array"

block ::= "{" statement* "}"

identifier ::= [a-zA-Z_][a-zA-Z0-9_]*
```

### Examples

**Variables and Types:**
```mt-lang
int x = 5
float pi = 3.14
string greeting = "hello"
string first_char = greeting[0]  // "h"
bool flag = true
array numbers = [1, 2, 3]
int first_num = numbers[0]  // 1

class Person {
    string name = "Alice"
}

Person p = new Person()
string first_letter = p.name[0]  // "A"
```

**Functions:**
```mt-lang
int add(int a, int b) {
    return a + b
}

func dynamic_func(string name) {
    print("Hello " + name)
}
```

**Classes and File I/O:**
```mt-lang
class Person {
    arg string name = "Unknown"  // constructor argument with default
    arg int age                   // constructor argument
    string nickname               // regular field

    void greet() {
        print("Hello, I'm " + this.name)
    }

    int getAge() {
        return this.age
    }

    func dynamicMethod() {
        print("This method has no explicit return type")
    }

    static void describe() {
        print("A Person class")
    }
}

Person p = new Person("Alice", 25)
p.greet()

// File I/O using stdlib.io
from stdlib.io use File
File file = new File("example.txt", "r")
string contents = file.read("io_test.txt")  // Reads actual file contents using libc functions
print(contents)  // Outputs: Hello from a different file!\nThis is io_test.txt

// Direct libc function access for stdlib implementation
string file = fopen("myfile.txt", "r")  // Direct libc calls
string buffer = malloc(1024)            // Memory management
fread(buffer, 1, 100, file)             // Read operations
fclose(file)                            // Close file
free(buffer)                            // Free memory
```

**Control Flow:**
```mt-lang
if (x > 0) {
    print("positive")
} else {
    print("non-positive")
}

while (i < 10) {
    print(i)
    set i = i + 1
}

for (item in array) {
    print(item)
}
```

**Modules and Imports:**
```mt-lang
// Import classes and functions from modules
from stdlib.io use File

// Use imported classes with constructor arguments
File file = new File("path.txt", "r")
string contents = file.read()
print(contents)
```

## Code Style Guidelines

### Python Version and Environment
- **Python version**: 3.14 (based on virtual environment)
- **Dependencies**: llvmlite for LLVM code generation
- **Virtual environment**: Always activate `venv/bin/activate` before running Python commands

### Import Organization
```python
# Standard library imports first
import sys
import os

# Third-party imports
import llvmlite.ir
import llvmlite.binding as llvm

# Local imports
from tokenizer import Tokenizer
from parser import Parser
from ast_nodes import *
```

### Class and Method Structure
```python
class ClassName:
    def __init__(self, param1, param2=None):
        self.param1 = param1
        self.param2 = param2 or []

    def method_name(self, arg1, arg2):
        # Method implementation
        pass
```

### Naming Conventions
- **Classes**: PascalCase (`SemanticAnalyzer`, `CodeGenerator`)
- **Methods/Functions**: snake_case (`analyze_program`, `create_main_function`)
- **Variables**: snake_case (`symbol_table`, `current_scope`)
- **Constants**: ALL_CAPS if any (rare in this codebase)
- **Files**: snake_case (`semantic.py`, `ast_nodes.py`)

### Type Hints
- **Optional but inconsistent**: Some classes use type hints, others don't
- **Common patterns**:
  ```python
  def __init__(self, tokens: list[Token], file_path=None):
      # Implementation

  def method(self, param: str) -> bool:
      # Implementation
  ```

### Error Handling
- **Custom error collection**: Use `self.errors = []` and `self.add_error(message)`
- **Position information**: Include line/column info in error messages
- **Error format**: `"Error message at line X, column Y in file.mtc"`

### Code Formatting
- **Indentation**: 4 spaces (not tabs)
- **Line length**: No strict limit, but keep lines readable
- **Blank lines**: Use blank lines to separate logical sections
- **String formatting**: Use f-strings for string interpolation
- **Comments**: Avoid comments in production code

### AST Node Patterns
```python
class NodeName:
    def __init__(self, param1, param2=None, line=None, column=None):
        self.param1 = param1
        self.param2 = param2
        self.line = line
        self.column = column

    def __repr__(self):
        return f"NodeName({self.param1}, {self.param2})"
```

### Control Flow and Logic
- **Early returns**: Use early returns to reduce nesting
- **List comprehensions**: Use for simple transformations
- **Dictionary lookups**: Prefer `dict.get(key, default)` over manual checks
- **Error accumulation**: Collect multiple errors before reporting

### File Organization
- **Main compiler**: `compiler.py` (entry point)
- **Core components**:
  - `tokenizer.py` - Lexical analysis
  - `parser.py` - Syntax analysis
  - `semantic.py` - Semantic analysis
  - `codegen.py` - LLVM code generation
  - `ast_nodes.py` - AST node definitions
- **Tests**: `tests/*.mtc` source files, compiled executables in `tests/`
- **Standard library**: `stdlib/*.mtc` for built-in functions

### Git Workflow
- **No specific branching strategy** documented
- **Commits**: Follow conventional commit format if established
- **Pull requests**: Create PRs for significant changes
- **No pre-commit hooks** configured

### Debugging and Development
- **Debug scripts**: `debug_parse.py`, `debug_tokens.py` for development
- **Test script**: `test.py` for quick testing
- **Manual verification**: Always compile and run after changes

### Performance Considerations
- **No optimization tools** configured
- **Memory usage**: Be mindful of large AST structures
- **Compilation speed**: LLVM compilation can be slow for large programs

### Security Notes
- **Input validation**: Always validate input files exist
- **Path handling**: Use absolute paths for error messages
- **External commands**: `clang` and system calls are used for linking

### Common Patterns
- **Visitor pattern**: Used in semantic analysis and code generation
- **Symbol tables**: Scope-based symbol resolution
- **Type checking**: Runtime type validation during semantic analysis
- **IR generation**: Direct LLVM IR construction via llvmlite

## Getting Started
1. Activate virtual environment: `source venv/bin/activate`
2. Run existing tests to verify setup
3. Make changes following the style guidelines above
4. Test compilation and execution
5. Verify no regressions in existing functionality</content>
<parameter name="filePath">/mnt/ssd/Coding/mt-lang/compiler/AGENTS.md