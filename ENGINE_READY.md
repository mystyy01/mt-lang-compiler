# MT-Lang Engine-Readiness Checklist

This checklist defines what must be true before MT-Lang is a practical choice for building a game engine.

## Exit Criteria

`Ready for engine development` means all items in Phases 0-3 are green, and at least 80% of Phase 4 is green.

## Phase 0: Build and Regression Gates (Required First)

- [x] Release compiler build target (`dist/mtc`)
- [x] Parser/semantic/codegen unit tests
- [x] End-to-end language tests
- [x] LSP binary build target (`dist/mtc_lsp`)
- [x] Single-command quality gate (`make check`) that runs all critical validation
- [x] Performance gate with baseline thresholds for compile/run time and memory
- [x] CI workflow to run build + tests on every push/PR

## Phase 1: Language Parity Stability

- [x] Full Python compiler feature parity test matrix (imports/classes/try-catch/dicts/arrays/io/builtins)
- [x] Error parity for major diagnostics (parser + semantic)
- [x] No Python fallback paths in compiler workflow or import resolution
- [x] Recursive import cycle handling with deterministic diagnostics
- [x] Fuzzing for tokenizer/parser crash resistance

## Phase 2: Performance and Memory Model

- [x] Dynamic array growth policy documented and benchmarked
- [x] Fixed-size stack arrays preferred by default where possible
- [x] Dictionary operation benchmarks and memory ceilings
- [x] Large-project compile-time benchmark suite
- [x] Optimization levels (`-O0`, `-O2`, optional `-O3`) validated for correctness

## Phase 3: Runtime + Toolchain Reliability

- [x] Stable runtime ABI and libc interop contract
- [x] Deterministic codegen for identical source
- [x] Cross-platform compile support matrix documented (Linux/macOS/Windows status)
- [x] Improved crash diagnostics for generated binaries
- [x] Versioned release process and changelog

## Phase 4: IDE/LSP Experience

- [x] Completion
- [x] Hover
- [x] Go to definition
- [x] References
- [x] Rename
- [x] Document/workspace symbols
- [x] Import ctrl+click resolution for modules/aliases
- [ ] Signature help
- [ ] Find implementations / type hierarchy (for classes/methods)
- [ ] Code actions (quick fixes for common semantic errors)
- [ ] Formatting support
- [ ] Semantic tokens (richer highlighting beyond grammar)

## Immediate Priority Order

1. Add LSP signature help.
2. Add semantic tokens (richer highlighting).
3. Add code actions for common semantic errors.
4. Add formatting support.
5. Add implementation/type hierarchy navigation.
