# mt-lang Compiler - Part 2: The Parser

## What are we building?

Remember our assembly line?

```
Your Code → [Tokenizer] ✓ → [Parser] → [Analyzer] → [Code Maker] → Running Program
```

The tokenizer chopped your code into tokens. Now the parser figures out the **structure** - how those tokens fit together.

---

## What is a parser?

The tokenizer gave us a flat list:
```
[KEYWORD: int, NAME: x, SYMBOL: =, NUMBER: 5]
```

But that doesn't tell us what this *means*. The parser builds a tree structure (called an AST - "Abstract Syntax Tree") that shows relationships:

```
VariableDeclaration
├── type: "int"
├── name: "x"
└── value: NumberLiteral(5)
```

Now we can see: "This is a variable declaration, the type is int, the name is x, and it's being set to the number 5."

This tree structure is what LLVM will eventually use to generate machine code.

---

## Part 2: The Parser (Days 4-7)

### Step 1: Set up the AST node classes

We need Python classes to represent different parts of code. Each class is a "node" in our tree.

Think of it like boxes that hold information:
- A "VariableDeclaration" box holds: the type, the name, and optionally a value
- A "FunctionDeclaration" box holds: the return type, the name, parameters, and the body
- A "BinaryExpression" box holds: the left side, the operator, and the right side

Create a new file called `ast_nodes.py` and add these node classes one at a time:

**Basic nodes:**
- [x] `NumberLiteral` - holds a number value (like `5` or `42`)
- [x] `StringLiteral` - holds a string value (like `"hello"`)
- [x] `Identifier` - holds a name (like `x` or `myVar`)

**Expression nodes** (things that produce a value):
- [x] `BinaryExpression` - two things with an operator between (like `x + 5` or `a == b`)
- [x] `CallExpression` - a function call (like `print(x)` or `range(10)`)
- [x] `MemberExpression` - accessing something with a dot (like `term.print` or `x.string()`)
- [x] `ArrayLiteral` - a list of values (like `["hello", "world"]`)
- [x] `TypeofExpression` - the typeof check (like `typeof(x)`)

**Statement nodes** (things that do something):
- [x] `VariableDeclaration` - declaring a variable (like `int x` or `array y = [...]`)
- [x] `FunctionDeclaration` - declaring a function (like `void hello() { ... }`)
- [x] `SetStatement` - assigning a value (like `set x = 5`)
- [x] `ReturnStatement` - returning from a function (like `return x + y`)
- [x] `IfStatement` - if/else blocks
- [x] `ForInStatement` - for loops (like `for (item in list) { ... }`)
- [x] `Block` - a group of statements inside `{ }`
- [x] `ExpressionStatement` - when you just call something (like `term.print(x)`)

**Top-level nodes:**
- [x] `ImportStatement` - imports (like `from essentials use terminal as term`)
- [x] `Program` - the root node that holds everything

---

### Step 2: Design each node class

Each node class needs:
1. An `__init__` method that stores the relevant data
2. A `__repr__` method so you can print it nicely (optional but helpful for debugging)

**Example - how to think about NumberLiteral:**

What information does a number need to hold?
- The actual value (like `5`)

That's it! So the class just stores that one thing.

**Example - how to think about BinaryExpression:**

What information does `x + 5` need to hold?
- The left side (`x` - which is an Identifier node)
- The operator (`+`)
- The right side (`5` - which is a NumberLiteral node)

**Example - how to think about IfStatement:**

What information does an if/else need to hold?
- The condition (an expression to check)
- The "then" body (what to do if true)
- The "else" body (what to do if false - this might be empty/None)

---

### Step 3: Create the basic node classes

Start with the simplest ones first:

- [ ] Create `NumberLiteral` class with a `value` property
- [ ] Create `StringLiteral` class with a `value` property
- [ ] Create `Identifier` class with a `name` property
- [ ] Create `BinaryExpression` class with `left`, `operator`, and `right` properties
- [ ] Create `ArrayLiteral` class with an `elements` property (a list)

Test tip: After creating each one, try making an instance in Python to make sure it works:
```
node = BinaryExpression(Identifier("x"), "+", NumberLiteral(5))
```

---

### Step 4: Create the statement node classes

- [ ] Create `VariableDeclaration` with `var_type`, `name`, and `value` (value can be None)
- [ ] Create `SetStatement` with `name` and `value`
- [ ] Create `ReturnStatement` with `value` (can be None for void functions)
- [ ] Create `Block` with `statements` (a list)
- [ ] Create `ExpressionStatement` with `expression`

---

### Step 5: Create the remaining node classes

- [ ] Create `CallExpression` with `callee` and `arguments` (a list)
- [ ] Create `MemberExpression` with `object` and `property`
- [ ] Create `TypeofExpression` with `argument`
- [ ] Create `IfStatement` with `condition`, `then_body`, and `else_body` (else can be None)
- [ ] Create `ForInStatement` with `variable`, `iterable`, and `body`
- [ ] Create `FunctionDeclaration` with `return_type`, `name`, `parameters`, and `body`
- [ ] Create `ImportStatement` with `module_path`, `symbol`, and `alias` (alias can be None)
- [ ] Create `Program` with `statements` (a list of all top-level stuff)

---

### Step 6: Set up the Parser class

Create a new file called `parser.py`.

The Parser needs:
- The list of tokens (from the tokenizer)
- A position tracker (like the tokenizer had)
- Helper methods to navigate the tokens

**Helper methods to create:**
- [x] `current_token()` - get the token at current position
- [x] `peek_token()` - look at the next token without moving
- [x] `advance()` - move to the next token
- [x] `is_at_end()` - check if we've gone through all tokens
- [x] `expect(token_type, value)` - check that current token matches what we expect, and advance (raise error if not)
- [x] `match(token_type, value)` - check if current token matches (without advancing)

---

### Step 7: Parse simple expressions (numbers, strings, names)

Start with the easiest parsing - single tokens that become nodes:

Create a method `parse_primary()` that:
- [x] If current token is a NUMBER → return a NumberLiteral node with that value
- [x] If current token is a STRING → return a StringLiteral node with that value
- [x] If current token is a NAME → return an Identifier node with that name
- [x] If current token is `(` → parse what's inside the parentheses and return it
- [x] If current token is `[` → parse an array literal

Don't forget to advance past each token you consume!

---

### Step 8: Parse function calls and member access

After parsing a primary expression, check if it's followed by `(` or `.`:

Create a method `parse_call_member()` that:
- [x] First, parse a primary expression
- [x] Then loop: while the next token is `(` or `.`:
  - If `(` → this is a function call, parse the arguments, create a CallExpression
  - If `.` → this is member access, parse the property name, create a MemberExpression
- [x] Return the result

This handles things like `term.print(x)` which is:
1. `term` (Identifier)
2. `.print` (becomes MemberExpression)
3. `(x)` (becomes CallExpression)

---

### Step 9: Parse binary expressions (math and comparisons)

This is where operator precedence matters. `1 + 2 * 3` should be `1 + (2 * 3)`, not `(1 + 2) * 3`.

We handle this by parsing in layers, from lowest to highest precedence:

1. `||` (or) - lowest
2. `&&` (and)
3. `==`, `!=` (equality)
4. `<`, `>`, `<=`, `>=` (comparison)
5. `+`, `-` (addition/subtraction)
6. `*`, `/` (multiplication/division) - highest

For each layer, create a method that:
- [ ] Parse the next-higher-precedence thing
- [ ] While you see an operator at this precedence level:
  - Save the operator
  - Parse another next-higher-precedence thing
  - Combine them into a BinaryExpression
- [ ] Return the result

Start with:
- [ ] `parse_expression()` - entry point, calls parse_or()
- [ ] `parse_or()` - handles `||`
- [ ] `parse_and()` - handles `&&`
- [ ] `parse_equality()` - handles `==`, `!=`
- [ ] `parse_comparison()` - handles `<`, `>`, `<=`, `>=`
- [ ] `parse_additive()` - handles `+`, `-`
- [ ] `parse_multiplicative()` - handles `*`, `/`

Each one calls the next one down. The bottom calls `parse_call_member()`.

---

### Step 10: Parse statements

Create methods for each statement type:

- [ ] `parse_variable_declaration()` - when you see `int`, `void`, `array`, etc.
- [ ] `parse_set_statement()` - when you see `set`
- [ ] `parse_return_statement()` - when you see `return`
- [ ] `parse_if_statement()` - when you see `if`
- [ ] `parse_for_statement()` - when you see `for`
- [ ] `parse_block()` - when you see `{`
- [ ] `parse_import_statement()` - when you see `from`

Then create a main `parse_statement()` that looks at the current token and calls the right one.

---

### Step 11: Parse the whole program

Create a method `parse()` that:
- [ ] Create an empty list for statements
- [ ] Loop until you reach the end:
  - Parse a statement (import, function, variable, or expression)
  - Add it to the list
- [ ] Return a Program node containing all the statements

---

### Step 12: Test it!

Update your test file to:
- [ ] Run the tokenizer
- [ ] Pass the tokens to the parser
- [ ] Print the resulting AST

Start simple:
1. `int x` → should give `VariableDeclaration(type="int", name="x", value=None)`
2. `5 + 3` → should give `BinaryExpression(left=5, op="+", right=3)`
3. Full `showcase.mtc` → should produce a complete AST

---

## Progress Checklist

- [ ] Basic nodes created (NumberLiteral, StringLiteral, Identifier)
- [ ] Expression nodes created (BinaryExpression, CallExpression, etc.)
- [ ] Statement nodes created (VariableDeclaration, IfStatement, etc.)
- [ ] Parser class with helper methods
- [ ] Can parse simple expressions
- [ ] Can parse function calls and member access
- [ ] Can parse binary expressions with correct precedence
- [ ] Can parse all statement types
- [ ] Can parse the full `showcase.mtc` into an AST
