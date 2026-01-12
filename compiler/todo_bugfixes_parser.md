# Parser Bugfix & TODO List

This document tracks known bugs, design flaws, and required improvements in the parser.
Priority roughly goes from **critical crashes** → **grammar correctness** → **future improvements**.

---

## 🚨 Critical Bugs (Must Fix)

### 1. Unsafe `current_token()` at EOF
- Accessing `self.tokens[self.position]` can raise `IndexError`
- Many parser functions call `current_token()` without checking EOF

**Fix**
- Return an explicit `EOF` token when `position >= len(tokens)`

---

### 2. `parse_primary()` may return `None`
- If token does not match any primary case, function silently falls through
- Causes crashes later in unrelated code

**Fix**
- Throw a `CompilerError` for unexpected tokens

---

### 3. Parentheses only parse primaries, not expressions
- `(1 + 2) * 3` parses incorrectly
- Same issue in `typeof(...)`

**Fix**
- Replace `parse_primary()` with `parse_expression()` inside grouping and `typeof`

---

### 4. Array literals only allow primaries
- `[1 + 2, foo(bar)]` is invalid under current parser

**Fix**
- Use `parse_expression()` instead of `parse_primary()` for array elements

---

### 5. Infinite loop risk in `parse_block()`
- If a statement fails to consume tokens, parser loops forever
- No EOF guard inside block parsing

**Fix**
- Detect EOF inside block and raise "Unterminated block" error

---

## ⚠️ Grammar & Logic Issues

### 6. Operator loops check `.value` directly
- Assumes token always exists and is an operator
- Can misbehave at EOF or with non-symbol tokens

**Fix**
- Use `match("SYMBOL", op)` instead of raw value checks

---

### 7. Expression statements have no terminator
- No semicolon or newline enforcement
- Can cause multiple expressions to merge incorrectly

**Fix**
- Introduce statement terminator or newline-aware parsing

---

### 8. Function parameter parsing is incorrect
- Allows expressions instead of identifiers
- Invalid: `int foo(1 + 2)`

**Fix**
- Restrict parameters to identifiers (and types if applicable)

---

### 9. Fragile function-vs-variable declaration detection
- Relies on peeking two tokens ahead
- Breaks with decorators or modifiers

**Fix**
- Use clearer grammar rules or pre-parse decorators/modifiers

---

### 10. Import alias does not consume identifier
- `as alias` does not `expect("NAME")` or advance properly

**Fix**
- Explicitly consume alias identifier

---

## 🧠 Missing Language Features

### 11. No unary expressions
- Missing support for `-x`, `!x`, `~x`

---

### 12. No assignment expressions
- Cannot parse chained assignments (`a = b = c`)
- `=` only exists in `set` statements

---

### 13. No error recovery
- First syntax error aborts entire parse

**Future**
- Add panic-mode recovery or synchronization points

---

## 🧪 Testing TODOs

- Parenthesized expressions
- Nested calls + member access
- Array literals with expressions
- Unterminated blocks
- EOF edge cases
- Invalid syntax error clarity

---

## 🛠 Future Refactors (Optional)

- Convert expression parsing to Pratt parser
- Centralize operator precedence table
- Add decorator parsing (`@entry`, etc.)
- Improve error messages with token context & location

---

## 📌 Notes

The current parser is structurally solid, but several correctness issues
stem from using `parse_primary()` where `parse_expression()` is required
and from missing EOF safeguards.

Fixing the critical section first will significantly improve stability.
