# Changelog

All notable changes to MT-Lang C++ compiler are documented in this file.

## [0.4.0] - 2026-02-12

### Added
- Full local quality gate (`make check`) including:
  - unit + e2e + parity + diagnostics + compiler integration tests
  - LSP smoke checks
  - perf/compile benchmarks and optimization-level validation
  - tokenizer/parser fuzz smoke
- Runtime/stdlib modules vendored in-repo (`stdlib/`), removing legacy external path assumptions.
- Import-cycle detection with deterministic diagnostics.
- LSP import ctrl+click definition support.
- New compiler flag: `--opt-level 0|1|2|3`.
- Compiler version flag: `--version`.
- Runtime ABI/performance/release documentation.

### Changed
- Import/module resolution now uses source ancestry, cwd, and `MTC_PATH`.
- Build now tracks header dependencies (`-MMD -MP`) for reliable incremental rebuilds.
- Runtime fatal paths use structured panic diagnostics.
