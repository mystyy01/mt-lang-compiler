#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_PATH="${ROOT_DIR}/dist/mtc"
E2E_DIR="${SCRIPT_DIR}/e2e"
MATRIX_FILE="${SCRIPT_DIR}/parity_matrix.tsv"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "missing compiler binary: ${BIN_PATH}" >&2
  exit 1
fi

if [[ ! -f "${MATRIX_FILE}" ]]; then
  echo "missing parity matrix file: ${MATRIX_FILE}" >&2
  exit 1
fi

run_with_timeout() {
  if command -v timeout >/dev/null 2>&1; then
    timeout 10s "$@"
  else
    "$@"
  fi
}

contains_tag() {
  local csv="$1"
  local tag="$2"
  [[ ",${csv}," == *",${tag},"* ]]
}

run_case() {
  local case_name="$1"
  local src_file="${E2E_DIR}/${case_name}.mtc"
  local expected_file="${E2E_DIR}/${case_name}.out"
  local input_file="${E2E_DIR}/${case_name}.in"
  local bin_file
  local compile_log
  local output_log
  local diff_log
  local rc

  if [[ ! -f "${src_file}" ]]; then
    echo "FAIL ${case_name} (missing source: ${src_file})"
    return 1
  fi
  if [[ ! -f "${expected_file}" ]]; then
    echo "FAIL ${case_name} (missing expected output: ${expected_file})"
    return 1
  fi

  bin_file="$(mktemp "/tmp/mtc_parity_${case_name}.bin.XXXXXX")"
  compile_log="$(mktemp "/tmp/mtc_parity_${case_name}.compile.XXXXXX.log")"
  output_log="$(mktemp "/tmp/mtc_parity_${case_name}.run.XXXXXX.log")"
  diff_log="$(mktemp "/tmp/mtc_parity_${case_name}.diff.XXXXXX.log")"

  if ! "${BIN_PATH}" "${src_file}" "${bin_file}" >"${compile_log}" 2>&1; then
    echo "FAIL ${case_name} (compile)"
    cat "${compile_log}"
    rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
    return 1
  fi

  if [[ -f "${input_file}" ]]; then
    if ! run_with_timeout "${bin_file}" <"${input_file}" >"${output_log}" 2>&1; then
      rc=$?
      echo "FAIL ${case_name} (run rc=${rc})"
      cat "${output_log}"
      rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
      return 1
    fi
  else
    if ! run_with_timeout "${bin_file}" >"${output_log}" 2>&1; then
      rc=$?
      echo "FAIL ${case_name} (run rc=${rc})"
      cat "${output_log}"
      rm -f "${bin_file}" "${bin_file}.o" "${compile_log}" "${output_log}" "${diff_log}"
      return 1
    fi
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

required_features=(
  imports
  classes
  try_catch
  dicts
  arrays
  io
  builtins
)

declare -A seen_features
pass_count=0
fail_count=0
case_count=0

while read -r case_name feature_csv _; do
  [[ -z "${case_name}" ]] && continue
  [[ "${case_name:0:1}" == "#" ]] && continue
  case_count=$((case_count + 1))

  if [[ -z "${feature_csv}" ]]; then
    echo "FAIL ${case_name} (missing feature tags)"
    fail_count=$((fail_count + 1))
    continue
  fi

  IFS=',' read -ra tags <<< "${feature_csv}"
  for tag in "${tags[@]}"; do
    trimmed="$(echo "${tag}" | xargs)"
    if [[ -n "${trimmed}" ]]; then
      seen_features["${trimmed}"]=1
    fi
  done

  if run_case "${case_name}"; then
    pass_count=$((pass_count + 1))
  else
    fail_count=$((fail_count + 1))
  fi
done < "${MATRIX_FILE}"

for feature in "${required_features[@]}"; do
  if [[ -z "${seen_features[${feature}]+x}" ]]; then
    echo "FAIL parity-matrix (missing required feature coverage: ${feature})"
    fail_count=$((fail_count + 1))
  fi
done

echo "Parity summary: ${pass_count} passed, ${fail_count} failed (${case_count} cases)"
if (( fail_count > 0 )); then
  exit 1
fi
