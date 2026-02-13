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

run_case_level() {
  local case_name="$1"
  local level="$2"
  local src_file="${E2E_DIR}/${case_name}.mtc"
  local expected_file="${E2E_DIR}/${case_name}.out"
  local input_file="${E2E_DIR}/${case_name}.in"
  local bin_file compile_log output_log diff_log rc

  bin_file="$(mktemp "/tmp/mtc_opt_${case_name}_O${level}.bin.XXXXXX")"
  compile_log="$(mktemp "/tmp/mtc_opt_${case_name}_O${level}.compile.XXXXXX.log")"
  output_log="$(mktemp "/tmp/mtc_opt_${case_name}_O${level}.run.XXXXXX.log")"
  diff_log="$(mktemp "/tmp/mtc_opt_${case_name}_O${level}.diff.XXXXXX.log")"

  if ! "${BIN_PATH}" --opt-level "${level}" "${src_file}" "${bin_file}" >"${compile_log}" 2>&1; then
    echo "FAIL ${case_name} O${level} (compile)"
    cat "${compile_log}"
    rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
    return 1
  fi

  if [[ -f "${input_file}" ]]; then
    if ! run_with_timeout "${bin_file}" <"${input_file}" >"${output_log}" 2>&1; then
      rc=$?
      echo "FAIL ${case_name} O${level} (run rc=${rc})"
      cat "${output_log}"
      rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
      return 1
    fi
  else
    if ! run_with_timeout "${bin_file}" >"${output_log}" 2>&1; then
      rc=$?
      echo "FAIL ${case_name} O${level} (run rc=${rc})"
      cat "${output_log}"
      rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
      return 1
    fi
  fi

  if ! diff -u "${expected_file}" "${output_log}" >"${diff_log}" 2>&1; then
    echo "FAIL ${case_name} O${level} (output mismatch)"
    cat "${diff_log}"
    rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
    return 1
  fi

  echo "PASS ${case_name} O${level}"
  rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
  return 0
}

levels=(0 2 3)
cases=(
  arith_control
  arrays_fixed_dynamic
  class_and_hasattr
  dict_ops
  imports_and_io
  read_split
  try_catch
)

pass_count=0
fail_count=0

for level in "${levels[@]}"; do
  for case_name in "${cases[@]}"; do
    if run_case_level "${case_name}" "${level}"; then
      pass_count=$((pass_count + 1))
    else
      fail_count=$((fail_count + 1))
    fi
  done
done

echo "Opt-level summary: ${pass_count} passed, ${fail_count} failed"
if (( fail_count > 0 )); then
  exit 1
fi
