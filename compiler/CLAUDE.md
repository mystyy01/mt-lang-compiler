# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and Run Commands

```bash
source venv/bin/activate
python compiler.py source.mtc output_name    # Compile .mtc file to executable
./output_name                                 # Run compiled binary
```

## Testing

```bash
# Run a single test
python compiler.py tests/test_file.mtc test_out && ./test_out

# Run all tests interactively (press Enter between each)
./run_all_tests.sh
```

Test files are in `tests/*.mtc`. No automated test runner exists.

## Architecture

The compiler follows a standard pipeline: Source → Tokens → AST → Semantic Analysis → LLVM IR → Executable

| Phase | File | Description |
|-------|------|-------------|
| Lexer | `tokenizer.py` | Tokenizes source into keywords, literals, operators |
| Parser | `parser.py` | Recursive descent parser, builds AST |
| AST | `ast_nodes.py` | Node classes for all language constructs |
| Semantic | `semantic.py` | Type checking, symbol resolution, scope management |
| Codegen | `codegen.py` | Generates LLVM IR via llvmlite, handles libc bindings |
| Main | `compiler.py` | Entry point, orchestrates pipeline, links with clang |

## Standard Library

Located in `stdlib/`:
- `io.mtc` - File class with read/write/append/readlines
- `math.mtc` - Array operations (min, max, sum), math functions
- `os.mtc` - System calls, file ops, directory ops
- `random.mtc` - Random number generation

## Language Reference

See `AGENTS.md` for complete grammar and syntax examples. Key points:

- Statically typed: `int`, `float`, `string`, `bool`, `array`, `void`
- Assignment uses `set`: `set x = x + 1`
- Classes use `arg` prefix for constructor parameters
- Imports: `from stdlib.io use File` or `from libc use printf`
- Dynamic functions: `func name(params) { }` (inferred return type)

## Debug Utilities

- `debug_tokens.py` - Test tokenizer output
- `debug_parse.py` - Test parser output
- `test.py` - Quick semantic analysis testing
