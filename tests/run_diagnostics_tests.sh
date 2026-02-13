#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_PATH="${ROOT_DIR}/dist/mtc"
DIAG_DIR="${SCRIPT_DIR}/diagnostics"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "missing compiler binary: ${BIN_PATH}" >&2
  exit 1
fi

if [[ ! -d "${DIAG_DIR}" ]]; then
  echo "missing diagnostics directory: ${DIAG_DIR}" >&2
  exit 1
fi

run_case() {
  local src_file="$1"
  local expected_file="$2"
  local case_name
  local out_bin
  local compile_log
  local actual_diag
  case_name="$(basename "${src_file}" .mtc)"

  out_bin="$(mktemp "/tmp/mtc_diag_${case_name}.bin.XXXXXX")"
  compile_log="$(mktemp "/tmp/mtc_diag_${case_name}.compile.XXXXXX.log")"
  actual_diag="$(mktemp "/tmp/mtc_diag_${case_name}.actual.XXXXXX.diag")"

  if "${BIN_PATH}" "${src_file}" "${out_bin}" >"${compile_log}" 2>&1; then
    echo "FAIL ${case_name} (expected compile failure)"
    cat "${compile_log}"
    rm -f "${out_bin}" "${out_bin}.o" "${compile_log}" "${actual_diag}"
    return 1
  fi

  sed -e "s|${ROOT_DIR}|<ROOT>|g" "${compile_log}" | rg '^Error:' > "${actual_diag}" || true

  if ! diff -u "${expected_file}" "${actual_diag}" >/dev/null 2>&1; then
    echo "FAIL ${case_name} (diagnostic snapshot mismatch)"
    echo "Expected:"
    cat "${expected_file}"
    echo "Actual:"
    cat "${actual_diag}"
    rm -f "${out_bin}" "${out_bin}.o" "${compile_log}" "${actual_diag}"
    return 1
  fi

  echo "PASS ${case_name}"
  rm -f "${out_bin}" "${out_bin}.o" "${compile_log}" "${actual_diag}"
  return 0
}

pass_count=0
fail_count=0

for src_file in "${DIAG_DIR}"/*.mtc; do
  case_name="$(basename "${src_file}" .mtc)"
  expected_file="${DIAG_DIR}/${case_name}.diag"
  if [[ ! -f "${expected_file}" ]]; then
    echo "FAIL ${case_name} (missing expected snapshot: ${expected_file})"
    fail_count=$((fail_count + 1))
    continue
  fi

  if run_case "${src_file}" "${expected_file}"; then
    pass_count=$((pass_count + 1))
  else
    fail_count=$((fail_count + 1))
  fi
done

echo "Diagnostics summary: ${pass_count} passed, ${fail_count} failed"
if (( fail_count > 0 )); then
  exit 1
fi
