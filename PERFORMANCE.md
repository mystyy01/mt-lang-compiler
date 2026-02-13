# MT-Lang Performance Notes

This document captures the current allocation policy for runtime collections and the benchmark gates used by CI/local validation.

## Dynamic Array Policy

Runtime layout for dynamic arrays:
- Header bytes: `24`
  - `len` (`i64`)
  - `cap` (`i64`)
  - `data` pointer (`i8*`)

Growth/shrink strategy:
- Initial capacity for literals/declarations: `max(4, initial_len)`
- `append` growth:
  - If `len == cap`, capacity doubles (`cap * 2`)
  - Capacity never drops below `4`
  - Reallocation uses `realloc`
- `pop` shrink:
  - Eligible when `cap > 8` and utilization is <= 25% (`new_len * 4 <= cap`)
  - Shrinks to `max(4, cap / 2)` using `realloc`

Rationale:
- Doubling amortizes append to O(1) average complexity.
- 25% shrink threshold avoids frequent grow/shrink oscillation.

## Dictionary Policy

Runtime layout for dicts:
- Header bytes: `32`
  - `len` (`i64`)
  - `cap` (`i64`)
  - keys pointer (`i8*`)
  - values pointer (`i8*`)

Growth strategy:
- Initial capacity: `max(4, initial_len)`
- On insert when `len == cap`, capacity doubles with floor of `4`
- Keys and values buffers are grown with `realloc`

## Benchmark Gates

The following scripts are part of the quality gate:
- `tests/run_perf_gate.sh`
  - Uses `tests/perf/perf_baseline.tsv`
  - Covers dynamic arrays, fixed arrays, and dict stress cases
- `tests/run_compile_benchmarks.sh`
  - Uses `tests/perf/compile_benchmark_baseline.tsv`
  - Generates synthetic small/medium/large projects and enforces compile time/RSS ceilings
- `tests/run_opt_level_validation.sh`
  - Validates output correctness across `--opt-level 0`, `2`, and `3`
  - Ensures optimization level changes do not regress behavior

Run all performance/correctness gates with:

```bash
make check
```
