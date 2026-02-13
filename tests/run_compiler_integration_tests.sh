#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_PATH="${ROOT_DIR}/dist/mtc"

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

run_negative_case() {
  local case_name="$1"
  local src_file="$2"
  local expected_substring="$3"
  local out_bin compile_log

  out_bin="$(mktemp "/tmp/mtc_negative_${case_name}.bin.XXXXXX")"
  compile_log="$(mktemp "/tmp/mtc_negative_${case_name}.compile.XXXXXX.log")"

  if "${BIN_PATH}" "${src_file}" "${out_bin}" >"${compile_log}" 2>&1; then
    echo "FAIL ${case_name} (expected compile failure)"
    cat "${compile_log}"
    rm -f "${out_bin}" "${out_bin}.o" "${compile_log}"
    return 1
  fi

  if ! grep -q "${expected_substring}" "${compile_log}"; then
    echo "FAIL ${case_name} (missing expected diagnostic: ${expected_substring})"
    cat "${compile_log}"
    rm -f "${out_bin}" "${out_bin}.o" "${compile_log}"
    return 1
  fi

  echo "PASS ${case_name}"
  rm -f "${out_bin}" "${out_bin}.o" "${compile_log}"
  return 0
}

run_mtc_path_case() {
  local tmp_root
  tmp_root="$(mktemp -d /tmp/mtc_path_case.XXXXXX)"
  local mod_dir="${tmp_root}/modules"
  local project_dir="${tmp_root}/project"
  local src_file="${project_dir}/main.mtc"
  local out_bin expected_file output_file compile_log

  mkdir -p "${mod_dir}" "${project_dir}"
  cat > "${mod_dir}/simplemath.mtc" <<'EOF'
int twice(int n) {
  return n * 2
}
EOF
  cat > "${src_file}" <<'EOF'
use simplemath as sm
print(sm.twice(21))
EOF

  out_bin="$(mktemp "/tmp/mtc_path_case.bin.XXXXXX")"
  expected_file="$(mktemp /tmp/mtc_path_case.expected.XXXXXX)"
  output_file="$(mktemp /tmp/mtc_path_case.output.XXXXXX)"
  compile_log="$(mktemp /tmp/mtc_path_case.compile.XXXXXX.log)"

  printf "42\n" > "${expected_file}"

  if ! MTC_PATH="${mod_dir}" "${BIN_PATH}" "${src_file}" "${out_bin}" >"${compile_log}" 2>&1; then
    echo "FAIL mtc_path_import (compile)"
    cat "${compile_log}"
    rm -rf "${tmp_root}"
    rm -f "${out_bin}" "${out_bin}.o" "${expected_file}" "${output_file}" "${compile_log}"
    return 1
  fi

  if ! run_with_timeout "${out_bin}" >"${output_file}" 2>&1; then
    echo "FAIL mtc_path_import (run)"
    cat "${output_file}"
    rm -rf "${tmp_root}"
    rm -f "${out_bin}" "${out_bin}.o" "${expected_file}" "${output_file}" "${compile_log}"
    return 1
  fi

  if ! diff -u "${expected_file}" "${output_file}" >/dev/null 2>&1; then
    echo "FAIL mtc_path_import (output mismatch)"
    diff -u "${expected_file}" "${output_file}" || true
    rm -rf "${tmp_root}"
    rm -f "${out_bin}" "${out_bin}.o" "${expected_file}" "${output_file}" "${compile_log}"
    return 1
  fi

  echo "PASS mtc_path_import"
  rm -rf "${tmp_root}"
  rm -f "${out_bin}" "${out_bin}.o" "${expected_file}" "${output_file}" "${compile_log}"
  return 0
}

pass_count=0
fail_count=0

if run_negative_case "import_cycle" "${SCRIPT_DIR}/negative/import_cycle_a.mtc" "Import cycle detected"; then
  pass_count=$((pass_count + 1))
else
  fail_count=$((fail_count + 1))
fi

if run_negative_case "bad_import_module" "${SCRIPT_DIR}/negative/bad_module_user.mtc" "Failed to parse imported module"; then
  pass_count=$((pass_count + 1))
else
  fail_count=$((fail_count + 1))
fi

if run_mtc_path_case; then
  pass_count=$((pass_count + 1))
else
  fail_count=$((fail_count + 1))
fi

echo "Compiler integration summary: ${pass_count} passed, ${fail_count} failed"
if [[ ${fail_count} -ne 0 ]]; then
  exit 1
fi
