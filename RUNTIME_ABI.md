# Runtime ABI and libc Interop Contract

This document defines the ABI contract used by generated MT-Lang binaries.

## ABI Version

- Global symbol: `@__mt_runtime_abi_version`
- Current value: `1`

## Runtime Globals

- `@__mt_char_pool : [8388608 x i8]`
- `@__mt_char_pool_index : i64`
- `@__mt_exc_jmp : i8*`
- `@__mt_exc_obj : i8*`
- `@__mt_exc_tag : i32`

## Runtime Helper Functions

- `@__mt_char(i8) -> i8*`
- `@__mt_runtime_panic(i8* msg, i32 code) -> void`

Panic codes:
- `1001` unhandled exception path
- `1002` invalid `int(...)` conversion

## Data Layout Contracts

### Dynamic Array Header (`24` bytes)
- offset `0`: `len` (`i64`)
- offset `8`: `cap` (`i64`)
- offset `16`: `data` pointer (`i8*`)

### Dictionary Header (`32` bytes)
- offset `0`: `len` (`i64`)
- offset `8`: `cap` (`i64`)
- offset `16`: keys pointer (`i8*`)
- offset `24`: values pointer (`i8*`)

## Exception Contract

- Throw writes:
  - object pointer to `@__mt_exc_obj`
  - class tag to `@__mt_exc_tag`
- If `@__mt_exc_jmp` is non-null, throw uses `longjmp`.
- If no active jump target exists, runtime panic is triggered.

## libc Interop Contract

`from libc use ...` uses signatures from `LIBC_FUNCTIONS`:
- source: `src/libc_functions.hpp`, `src/libc_functions.cpp`
- semantic type mapping:
  - `int -> int`
  - `ptr -> string`
  - `void -> void`
  - `float -> float`

Generated IR includes declarations for required runtime/builtin libc functions (`printf`, `exit`, `malloc`, `realloc`, `strlen`, etc.) and user-requested libc imports.
