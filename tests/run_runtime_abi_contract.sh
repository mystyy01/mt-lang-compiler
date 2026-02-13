#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_PATH="${ROOT_DIR}/dist/mtc"

ABI_SAMPLE="${SCRIPT_DIR}/abi/runtime_abi_sample.mtc"
LIBC_SAMPLE="${SCRIPT_DIR}/abi/libc_interop_sample.mtc"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "missing compiler binary: ${BIN_PATH}" >&2
  exit 1
fi

expect_pattern() {
  local pattern="$1"
  local file="$2"
  local label="$3"
  if ! rg -q --no-line-number --multiline "${pattern}" "${file}"; then
    echo "FAIL abi-contract (${label})"
    return 1
  fi
  return 0
}

ir_file="$(mktemp /tmp/mtc_abi_contract.XXXXXX.ll)"
abi_bin="$(mktemp /tmp/mtc_abi_contract.XXXXXX.bin)"
abi_log="$(mktemp /tmp/mtc_abi_contract.XXXXXX.log)"

if ! "${BIN_PATH}" --emit-ir "${ABI_SAMPLE}" "${abi_bin}" >"${ir_file}" 2>"${abi_log}"; then
  echo "FAIL abi-contract (compile)"
  cat "${abi_log}"
  rm -f "${ir_file}" "${abi_bin}" "${abi_bin}.o" "${abi_log}"
  exit 1
fi

fail_count=0

expect_pattern "@__mt_runtime_abi_version = constant i32 1" "${ir_file}" "abi-version" || fail_count=$((fail_count + 1))
expect_pattern "@__mt_exc_jmp = internal global i8\\* null" "${ir_file}" "exception-jmp-global" || fail_count=$((fail_count + 1))
expect_pattern "@__mt_exc_obj = internal global i8\\* null" "${ir_file}" "exception-obj-global" || fail_count=$((fail_count + 1))
expect_pattern "@__mt_exc_tag = internal global i32 0" "${ir_file}" "exception-tag-global" || fail_count=$((fail_count + 1))
expect_pattern "define void @__mt_runtime_panic\\(i8\\* %msg, i32 %code\\)" "${ir_file}" "runtime-panic-function" || fail_count=$((fail_count + 1))
expect_pattern "declare i32 @setjmp\\(i8\\*\\)" "${ir_file}" "setjmp-declare" || fail_count=$((fail_count + 1))
expect_pattern "declare void @longjmp\\(i8\\*, i32\\)" "${ir_file}" "longjmp-declare" || fail_count=$((fail_count + 1))
expect_pattern "declare i32 @printf\\(i8\\*, \\.\\.\\.\\)" "${ir_file}" "printf-declare" || fail_count=$((fail_count + 1))
expect_pattern "declare void @exit\\(i32\\)" "${ir_file}" "exit-declare" || fail_count=$((fail_count + 1))
expect_pattern "call i8\\* @malloc\\(i64 24\\)" "${ir_file}" "dynamic-array-header-size" || fail_count=$((fail_count + 1))
expect_pattern "call i8\\* @malloc\\(i64 32\\)" "${ir_file}" "dict-header-size" || fail_count=$((fail_count + 1))
expect_pattern "getelementptr inbounds i8, i8\\* .* i64 16" "${ir_file}" "array-data-offset-16" || fail_count=$((fail_count + 1))
expect_pattern "getelementptr inbounds i8, i8\\* .* i64 24" "${ir_file}" "dict-values-offset-24" || fail_count=$((fail_count + 1))

libc_bin="$(mktemp /tmp/mtc_abi_libc.XXXXXX.bin)"
libc_log="$(mktemp /tmp/mtc_abi_libc.XXXXXX.log)"
libc_out="$(mktemp /tmp/mtc_abi_libc.XXXXXX.out)"

if ! "${BIN_PATH}" "${LIBC_SAMPLE}" "${libc_bin}" >"${libc_log}" 2>&1; then
  echo "FAIL abi-contract (libc-compile)"
  cat "${libc_log}"
  fail_count=$((fail_count + 1))
else
  if ! "${libc_bin}" >"${libc_out}" 2>&1; then
    echo "FAIL abi-contract (libc-run)"
    cat "${libc_out}"
    fail_count=$((fail_count + 1))
  elif ! diff -u <(printf "4\n") "${libc_out}" >/dev/null 2>&1; then
    echo "FAIL abi-contract (libc-output)"
    cat "${libc_out}"
    fail_count=$((fail_count + 1))
  fi
fi

rm -f "${ir_file}" "${abi_bin}" "${abi_bin}.o" "${abi_log}" "${libc_bin}" "${libc_bin}.o" "${libc_log}" "${libc_out}"

if (( fail_count > 0 )); then
  exit 1
fi

echo "PASS runtime abi contract"
