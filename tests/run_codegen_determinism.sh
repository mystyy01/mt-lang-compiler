#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_PATH="${ROOT_DIR}/dist/mtc"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "missing compiler binary: ${BIN_PATH}" >&2
  exit 1
fi

cases=(
  "${SCRIPT_DIR}/e2e/class_and_hasattr.mtc"
  "${SCRIPT_DIR}/e2e/dict_ops.mtc"
  "${SCRIPT_DIR}/e2e/try_catch.mtc"
  "${SCRIPT_DIR}/abi/runtime_abi_sample.mtc"
)

pass_count=0
fail_count=0

for src_file in "${cases[@]}"; do
  case_name="$(basename "${src_file}" .mtc)"
  out_bin="$(mktemp "/tmp/mtc_determinism_${case_name}.XXXXXX.bin")"
  ir1="$(mktemp "/tmp/mtc_determinism_${case_name}.1.XXXXXX.ll")"
  ir2="$(mktemp "/tmp/mtc_determinism_${case_name}.2.XXXXXX.ll")"
  ir3="$(mktemp "/tmp/mtc_determinism_${case_name}.3.XXXXXX.ll")"
  log1="$(mktemp "/tmp/mtc_determinism_${case_name}.1.XXXXXX.log")"
  log2="$(mktemp "/tmp/mtc_determinism_${case_name}.2.XXXXXX.log")"
  log3="$(mktemp "/tmp/mtc_determinism_${case_name}.3.XXXXXX.log")"

  ok=1
  if ! "${BIN_PATH}" --emit-ir "${src_file}" "${out_bin}" >"${ir1}" 2>"${log1}"; then
    ok=0
  fi
  if (( ok == 1 )) && ! "${BIN_PATH}" --emit-ir "${src_file}" "${out_bin}" >"${ir2}" 2>"${log2}"; then
    ok=0
  fi
  if (( ok == 1 )) && ! "${BIN_PATH}" --emit-ir "${src_file}" "${out_bin}" >"${ir3}" 2>"${log3}"; then
    ok=0
  fi

  if (( ok == 0 )); then
    echo "FAIL ${case_name} (compile)"
    cat "${log1}" "${log2}" "${log3}" 2>/dev/null || true
    fail_count=$((fail_count + 1))
    rm -f "${out_bin}" "${out_bin}.o" "${ir1}" "${ir2}" "${ir3}" "${log1}" "${log2}" "${log3}"
    continue
  fi

  if ! diff -u "${ir1}" "${ir2}" >/dev/null 2>&1 || ! diff -u "${ir1}" "${ir3}" >/dev/null 2>&1; then
    echo "FAIL ${case_name} (non-deterministic IR)"
    fail_count=$((fail_count + 1))
  else
    echo "PASS ${case_name}"
    pass_count=$((pass_count + 1))
  fi

  rm -f "${out_bin}" "${out_bin}.o" "${ir1}" "${ir2}" "${ir3}" "${log1}" "${log2}" "${log3}"
done

echo "Determinism summary: ${pass_count} passed, ${fail_count} failed"
if (( fail_count > 0 )); then
  exit 1
fi
