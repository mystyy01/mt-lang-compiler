# mt-lang Compiler - LLVM Code Generation

## Completed

- [x] Basic setup (module, int_type, void_type)
- [x] create_main_function()
- [x] NumberLiteral → LLVM constant
- [x] BinaryExpression → add, sub, mul, div
- [x] Compile to object file and link

---

## Current: Variables

### VariableDeclaration (`int x = 5`)

- [ ] Allocate stack space with `alloca`
- [ ] Save the pointer in `self.variables` dictionary
- [ ] If there's an initial value, generate it and `store` it

### Identifier (`x`)

- [ ] Look up the pointer in `self.variables`
- [ ] Load and return the value with `load`

### SetStatement (`set x = 10`)

- [ ] Look up the pointer in `self.variables`
- [ ] Generate code for the new value
- [ ] Store it with `store`

---

## Next: Functions

### FunctionDeclaration (`int foo(a, b) { ... }`)

- [ ] Create function type with parameters
- [ ] Create function in module
- [ ] Create entry block
- [ ] Handle parameters (store them as variables)
- [ ] Generate code for body statements
- [ ] Handle return statement

### CallExpression (`foo(1, 2)`)

- [ ] Look up the function
- [ ] Generate code for each argument
- [ ] Call the function with `builder.call`

---

## Later: Control Flow

### IfStatement

- [ ] Generate condition
- [ ] Create "then" and "else" blocks
- [ ] Create "merge" block (where both paths join)
- [ ] Use conditional branch

### ForInStatement

- [ ] This is tricky - needs iterator pattern
- [ ] Maybe start with a simpler while loop first

---

## Later: Print Function

- [ ] Declare external `printf` function
- [ ] Handle string literals (i8 pointer)
- [ ] Format strings for integers

---

## LLVM Cheat Sheet

**Types:**
- `llvmlite.ir.IntType(32)` - 32-bit integer
- `llvmlite.ir.VoidType()` - void
- `llvmlite.ir.IntType(8)` - byte (for strings)
- `llvmlite.ir.PointerType(type)` - pointer to type

**Instructions:**
- `builder.alloca(type, name="x")` - allocate stack space, returns pointer
- `builder.store(value, pointer)` - write value to pointer
- `builder.load(type, pointer, name="x")` - read value from pointer
- `builder.add(a, b, name="tmp")` - addition
- `builder.sub(a, b, name="tmp")` - subtraction
- `builder.mul(a, b, name="tmp")` - multiplication
- `builder.sdiv(a, b, name="tmp")` - signed division
- `builder.ret(value)` - return from function
- `builder.call(func, args, name="tmp")` - call a function

**Constants:**
- `llvmlite.ir.Constant(type, value)` - create a constant
