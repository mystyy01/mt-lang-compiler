# Platform Support Matrix

Current status reflects the tested state of this repository as of 2026-02-12.

## Compiler Toolchain

| Platform | Status | Notes |
|---|---|---|
| Linux (x86_64) | Supported | Primary validated target in local and CI runs. |
| macOS (arm64/x86_64) | Expected | Requires `clang`, `make`, and equivalent `time` utility (or fallback path in scripts). |
| Windows (MSYS2/WSL) | Partial | Native MSVC pipeline is not yet validated; WSL/Linux path works. |

## Runtime/Generated Binary

| Platform | Status | Notes |
|---|---|---|
| Linux (x86_64) | Supported | Baseline runtime/perf measurements collected here. |
| macOS | Expected | LLVM IR -> clang flow should work; not currently in CI matrix. |
| Windows | Partial | Recommend WSL until native toolchain and path handling are validated. |

## LSP + VS Code Extension

| Platform | Status | Notes |
|---|---|---|
| Linux | Supported | `dist/mtc_lsp` validated with smoke tests. |
| macOS | Expected | Requires local build from source. |
| Windows | Partial | Extension should work with a built `mtc_lsp`; packaging path still manual. |
