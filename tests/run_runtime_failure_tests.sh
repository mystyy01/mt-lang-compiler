#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_PATH="${ROOT_DIR}/dist/mtc"
FAIL_DIR="${SCRIPT_DIR}/runtime_failures"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "missing compiler binary: ${BIN_PATH}" >&2
  exit 1
fi

run_with_timeout() {
  if command -v timeout >/dev/null 2>&1; then
    timeout 10s "$@"
  else
    "$@"
  fi
}

run_case() {
  local src_file="$1"
  local expect_file="$2"
  local case_name
  case_name="$(basename "${src_file}" .mtc)"

  local bin_file compile_log run_log expected_substring
  bin_file="$(mktemp "/tmp/mtc_runtime_fail_${case_name}.XXXXXX.bin")"
  compile_log="$(mktemp "/tmp/mtc_runtime_fail_${case_name}.XXXXXX.compile.log")"
  run_log="$(mktemp "/tmp/mtc_runtime_fail_${case_name}.XXXXXX.run.log")"

  if ! "${BIN_PATH}" "${src_file}" "${bin_file}" >"${compile_log}" 2>&1; then
    echo "FAIL ${case_name} (compile)"
    cat "${compile_log}"
    rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${run_log}"
    return 1
  fi

  if run_with_timeout "${bin_file}" >"${run_log}" 2>&1; then
    echo "FAIL ${case_name} (expected runtime failure)"
    cat "${run_log}"
    rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${run_log}"
    return 1
  fi

  expected_substring="$(cat "${expect_file}")"
  if ! rg -q --fixed-strings "${expected_substring}" "${run_log}"; then
    echo "FAIL ${case_name} (missing expected panic text)"
    echo "Expected substring: ${expected_substring}"
    echo "Actual output:"
    cat "${run_log}"
    rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${run_log}"
    return 1
  fi

  echo "PASS ${case_name}"
  rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${run_log}"
  return 0
}

pass_count=0
fail_count=0

for src_file in "${FAIL_DIR}"/*.mtc; do
  case_name="$(basename "${src_file}" .mtc)"
  expect_file="${FAIL_DIR}/${case_name}.expect"
  if [[ ! -f "${expect_file}" ]]; then
    echo "FAIL ${case_name} (missing expectation file ${expect_file})"
    fail_count=$((fail_count + 1))
    continue
  fi

  if run_case "${src_file}" "${expect_file}"; then
    pass_count=$((pass_count + 1))
  else
    fail_count=$((fail_count + 1))
  fi
done

echo "Runtime-failure summary: ${pass_count} passed, ${fail_count} failed"
if (( fail_count > 0 )); then
  exit 1
fi
