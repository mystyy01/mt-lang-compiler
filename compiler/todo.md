# mt-lang Compiler Progress

## Completed Phases

### Phase 1: Tokenizer ✓
- Tokenizes source code into tokens (keywords, names, numbers, strings, symbols)
- Handles comments
- File: `tokenizer.py`

### Phase 2: Parser ✓
- Builds AST from tokens
- Handles all expressions (binary, calls, member access, typeof, arrays)
- Handles all statements (if, for, set, return, variable/function declarations, imports)
- Files: `parser.py`, `ast_nodes.py`

### Phase 3: Semantic Analysis (In Progress)
- Symbol table with scopes ✓
- Undeclared variable checking ✓
- Scope management for functions, if, for ✓
- File: `semantic.py`

**Still to do for semantic analysis:**
- [ ] Type checking (does `int x = "hello"` produce an error?)
- [ ] Function return type validation (does `int foo()` actually return an int?)
- [ ] Check function calls have correct number of arguments
- [ ] Check that `return` only appears inside functions
- [ ] Warn about unused variables (optional)

---

## Next Phase: Code Generation (LLVM)

This is where we turn the AST into actual machine code using LLVM.

### Step 1: Set up llvmlite

- [ ] Install llvmlite: `pip install llvmlite`
- [ ] Create `codegen.py`
- [ ] Set up basic LLVM module, builder, and execution engine

### Step 2: Generate code for simple expressions

- [ ] NumberLiteral → LLVM integer constant
- [ ] BinaryExpression → LLVM add/sub/mul/div instructions
- [ ] Test: `5 + 3` should produce LLVM IR that evaluates to 8

### Step 3: Generate code for variables

- [ ] VariableDeclaration → LLVM alloca (allocate stack space)
- [ ] Identifier → LLVM load (read from variable)
- [ ] SetStatement → LLVM store (write to variable)

### Step 4: Generate code for functions

- [ ] FunctionDeclaration → LLVM function definition
- [ ] ReturnStatement → LLVM return instruction
- [ ] CallExpression → LLVM function call

### Step 5: Generate code for control flow

- [ ] IfStatement → LLVM conditional branch
- [ ] ForInStatement → LLVM loop with phi nodes (this is tricky)

### Step 6: Link with runtime

- [ ] Create external functions for print, etc.
- [ ] Link everything together
- [ ] Compile to executable or run with JIT

---

## Progress Checklist

- [x] Tokenizer complete
- [x] Parser complete
- [x] Basic semantic analysis
- [ ] Full type checking
- [ ] LLVM code generation
- [ ] Can compile `showcase.mtc` to executable
