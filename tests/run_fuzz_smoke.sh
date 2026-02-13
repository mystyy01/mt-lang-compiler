#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
FUZZ_BIN="${ROOT_DIR}/dist/mtc_fuzz_parser"

if [[ ! -x "${FUZZ_BIN}" ]]; then
  echo "missing fuzz parser binary: ${FUZZ_BIN}" >&2
  exit 1
fi

run_with_timeout() {
  if command -v timeout >/dev/null 2>&1; then
    timeout 2s "$@"
  else
    "$@"
  fi
}

run_one() {
  local label="$1"
  local file_path="$2"
  local log_file rc
  log_file="$(mktemp "/tmp/mtc_fuzz_${label}.XXXXXX.log")"

  if ! run_with_timeout "${FUZZ_BIN}" "${file_path}" >"${log_file}" 2>&1; then
    rc=$?
    echo "FAIL ${label} (rc=${rc})"
    cat "${log_file}"
    rm -f "${log_file}"
    return 1
  fi

  rm -f "${log_file}"
  return 0
}

generate_random_source() {
  local out_file="$1"
  local lines=$((RANDOM % 24 + 1))

  local keywords=(int float string bool array dict class if else while for in return set try catch throw use from as dynamic new)
  local symbols=("{" "}" "(" ")" "[" "]" "," "." "+" "-" "*" "/" "%" "<" ">" "==" "!=" "<=" ">=" "=" ":" ";")

  : > "${out_file}"
  for ((l = 0; l < lines; ++l)); do
    local tokens=$((RANDOM % 16 + 1))
    local line=""
    for ((t = 0; t < tokens; ++t)); do
      case $((RANDOM % 8)) in
        0)
          line+="${keywords[RANDOM % ${#keywords[@]}]} "
          ;;
        1)
          line+="${symbols[RANDOM % ${#symbols[@]}]} "
          ;;
        2)
          line+="$((RANDOM % 100000)) "
          ;;
        3)
          line+="\"s$((RANDOM % 999))\" "
          ;;
        4)
          line+="name$((RANDOM % 999)) "
          ;;
        5)
          line+="\n"
          ;;
        6)
          line+="# comment $((RANDOM % 999)) "
          ;;
        *)
          line+=" "
          ;;
      esac
    done
    printf "%b\n" "${line}" >> "${out_file}"
  done
}

pass_count=0
fail_count=0

# Deterministic seed for reproducibility.
RANDOM=1337

# Corpus replay from known fixtures.
while IFS= read -r corpus_file; do
  [[ -z "${corpus_file}" ]] && continue
  label="$(basename "${corpus_file}")"
  if run_one "corpus_${label}" "${corpus_file}"; then
    pass_count=$((pass_count + 1))
  else
    fail_count=$((fail_count + 1))
  fi
done < <(find "${SCRIPT_DIR}/e2e" "${SCRIPT_DIR}/negative" "${SCRIPT_DIR}/diagnostics" -type f -name '*.mtc' | sort)

# Randomized fuzz corpus.
for i in $(seq 1 200); do
  fuzz_file="$(mktemp "/tmp/mtc_fuzz_case_${i}.XXXXXX.mtc")"
  generate_random_source "${fuzz_file}"
  if run_one "random_${i}" "${fuzz_file}"; then
    pass_count=$((pass_count + 1))
  else
    fail_count=$((fail_count + 1))
  fi
  rm -f "${fuzz_file}"
done

echo "Fuzz summary: ${pass_count} passed, ${fail_count} failed"
if (( fail_count > 0 )); then
  exit 1
fi
