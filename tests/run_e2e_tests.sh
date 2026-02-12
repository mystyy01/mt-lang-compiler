#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_PATH="${ROOT_DIR}/dist/mtc"
E2E_DIR="${SCRIPT_DIR}/e2e"

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
  local case_name="$1"
  local src_file="${E2E_DIR}/${case_name}.mtc"
  local expected_file="${E2E_DIR}/${case_name}.out"
  local bin_file
  local compile_log
  local output_log
  local diff_log
  local rc

  bin_file="$(mktemp "/tmp/mtc_e2e_${case_name}.bin.XXXXXX")"
  compile_log="$(mktemp "/tmp/mtc_e2e_${case_name}.compile.XXXXXX.log")"
  output_log="$(mktemp "/tmp/mtc_e2e_${case_name}.run.XXXXXX.log")"
  diff_log="$(mktemp "/tmp/mtc_e2e_${case_name}.diff.XXXXXX.log")"

  if ! "${BIN_PATH}" "${src_file}" "${bin_file}" >"${compile_log}" 2>&1; then
    echo "FAIL ${case_name} (compile)"
    cat "${compile_log}"
    rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
    return 1
  fi

  if ! run_with_timeout "${bin_file}" >"${output_log}" 2>&1; then
    rc=$?
    echo "FAIL ${case_name} (run rc=${rc})"
    cat "${output_log}"
    rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
    return 1
  fi

  if ! diff -u "${expected_file}" "${output_log}" >"${diff_log}" 2>&1; then
    echo "FAIL ${case_name} (output mismatch)"
    cat "${diff_log}"
    rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
    return 1
  fi

  echo "PASS ${case_name}"
  rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
  return 0
}

cases=(
  arith_control
  arrays_fixed_dynamic
  class_and_hasattr
  classof_basic
  dict_ops
  dict_literal
  imports_and_io
  simple_import_alias
  string_casts
  try_catch
  try_return_restore
  typed_array_methods
)

pass_count=0
fail_count=0

for case_name in "${cases[@]}"; do
  if run_case "${case_name}"; then
    pass_count=$((pass_count + 1))
  else
    fail_count=$((fail_count + 1))
  fi
done

echo "E2E summary: ${pass_count} passed, ${fail_count} failed"

if [[ ${fail_count} -ne 0 ]]; then
  exit 1
fi
