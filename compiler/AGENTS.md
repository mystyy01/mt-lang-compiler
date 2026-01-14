# MT Lang Compiler - Agent Development Guide

## Build/Test Commands
- **Compile source file**: `python compiler.py source.mtc output_name`
- **Run single test**: Create test file, then `python compiler.py test_file.mtc test_output`
- **Run basic test**: `python test.py` (simple tokenizer/parser test)
- **No formal test framework** - use individual .mtc files with compiler

## Code Style Guidelines

### Imports
- Standard library imports first, then local imports
- No `from module import *` except for `ast_nodes` which is used everywhere
- Keep imports minimal and explicit

### Formatting & Types
- **4 spaces for indentation** (no tabs)
- **Type hints required** for function parameters: `def parse(self, tokens: list[Token])`
- **Snake_case** for variables and functions: `parse_statement()`, `current_token`
- **PascalCase** for classes: `SemanticAnalyzer`, `BinaryExpression`

### Error Handling
- Use custom `CompilerError` exception from `tokenizer.py`
- Collect semantic errors in `self.errors` list rather than raising immediately
- Provide clear error messages with context

### Architecture
- **tokenizer.py**: Lexical analysis → tokens
- **parser.py**: Tokens → AST nodes  
- **semantic.py**: AST analysis → symbol table, type checking
- **codegen.py**: AST → LLVM IR
- **ast_nodes.py**: All AST node definitions (no imports)

### Key Patterns
- Parser uses recursive descent with methods like `parse_expression()`, `parse_statement()`
- All AST nodes have `__repr__` methods for debugging
- Functions return LLVM values, statements return None
- Use `self.builder` for LLVM IR generation in codegen

## Project Structure
- `stdlib/`: Standard library modules (.mtc files)
- `.mtc` files: Source code test files
- `out`: Generated executable (created by compiler)