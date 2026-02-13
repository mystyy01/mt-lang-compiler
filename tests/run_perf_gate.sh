#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_PATH="${ROOT_DIR}/dist/mtc"
PERF_DIR="${SCRIPT_DIR}/perf"
BASELINE_FILE="${PERF_DIR}/perf_baseline.tsv"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "missing compiler binary: ${BIN_PATH}" >&2
  exit 1
fi

if [[ ! -f "${BASELINE_FILE}" ]]; then
  echo "missing performance baseline file: ${BASELINE_FILE}" >&2
  exit 1
fi

TIME_BIN=""
if [[ -x /usr/bin/time ]]; then
  TIME_BIN="/usr/bin/time"
elif command -v gtime >/dev/null 2>&1; then
  TIME_BIN="$(command -v gtime)"
fi

to_ms() {
  local seconds="$1"
  awk -v s="${seconds}" 'BEGIN { printf "%.0f", s * 1000 }'
}

read_peak_rss_kb() {
  local pid="$1"
  if [[ ! -r "/proc/${pid}/status" ]]; then
    echo "0"
    return
  fi
  awk '/VmRSS:/ { print $2; found=1; exit } END { if (!found) print 0 }' "/proc/${pid}/status"
}

run_timed_fallback() {
  local metric_file="$1"
  shift
  local start_ns end_ns elapsed_seconds peak_rss current_rss rc

  start_ns="$(date +%s%N)"
  "$@" &
  local pid=$!
  peak_rss=0

  while kill -0 "${pid}" >/dev/null 2>&1; do
    current_rss="$(read_peak_rss_kb "${pid}")"
    if (( current_rss > peak_rss )); then
      peak_rss="${current_rss}"
    fi
    sleep 0.01
  done

  wait "${pid}"
  rc=$?
  end_ns="$(date +%s%N)"
  elapsed_seconds="$(awk -v d="$((end_ns - start_ns))" 'BEGIN { printf "%.6f", d / 1000000000.0 }')"
  printf "%s %s\n" "${elapsed_seconds}" "${peak_rss}" > "${metric_file}"
  return "${rc}"
}

run_timed() {
  local metric_file="$1"
  shift
  if [[ -n "${TIME_BIN}" ]]; then
    "${TIME_BIN}" -f "%e %M" -o "${metric_file}" "$@"
    return $?
  fi
  run_timed_fallback "${metric_file}" "$@"
}

pass_count=0
fail_count=0

while read -r case_name compile_ms_max compile_rss_max run_ms_max run_rss_max; do
  [[ -z "${case_name}" ]] && continue
  [[ "${case_name:0:1}" == "#" ]] && continue

  src_file="${PERF_DIR}/${case_name}.mtc"
  if [[ ! -f "${src_file}" ]]; then
    echo "FAIL ${case_name} (missing source: ${src_file})"
    fail_count=$((fail_count + 1))
    continue
  fi

  bin_file="$(mktemp "/tmp/mtc_perf_${case_name}.bin.XXXXXX")"
  compile_metrics="$(mktemp "/tmp/mtc_perf_${case_name}.compile.XXXXXX.metrics")"
  run_metrics="$(mktemp "/tmp/mtc_perf_${case_name}.run.XXXXXX.metrics")"
  compile_log="$(mktemp "/tmp/mtc_perf_${case_name}.compile.XXXXXX.log")"
  run_log="$(mktemp "/tmp/mtc_perf_${case_name}.run.XXXXXX.log")"

  if ! run_timed "${compile_metrics}" "${BIN_PATH}" "${src_file}" "${bin_file}" >"${compile_log}" 2>&1; then
    echo "FAIL ${case_name} (compile)"
    cat "${compile_log}"
    rm -f "${bin_file}" "${bin_file}.o" "${compile_metrics}" "${run_metrics}" "${compile_log}" "${run_log}"
    fail_count=$((fail_count + 1))
    continue
  fi

  if ! run_timed "${run_metrics}" "${bin_file}" >"${run_log}" 2>&1; then
    echo "FAIL ${case_name} (run)"
    cat "${run_log}"
    rm -f "${bin_file}" "${bin_file}.o" "${compile_metrics}" "${run_metrics}" "${compile_log}" "${run_log}"
    fail_count=$((fail_count + 1))
    continue
  fi

  read -r compile_seconds compile_rss_kb < "${compile_metrics}"
  read -r run_seconds run_rss_kb < "${run_metrics}"
  compile_ms="$(to_ms "${compile_seconds}")"
  run_ms="$(to_ms "${run_seconds}")"

  failed=0
  if (( compile_ms > compile_ms_max )); then
    echo "FAIL ${case_name} compile_ms=${compile_ms} > max=${compile_ms_max}"
    failed=1
  fi
  if (( compile_rss_kb > compile_rss_max )); then
    echo "FAIL ${case_name} compile_rss_kb=${compile_rss_kb} > max=${compile_rss_max}"
    failed=1
  fi
  if (( run_ms > run_ms_max )); then
    echo "FAIL ${case_name} run_ms=${run_ms} > max=${run_ms_max}"
    failed=1
  fi
  if (( run_rss_kb > run_rss_max )); then
    echo "FAIL ${case_name} run_rss_kb=${run_rss_kb} > max=${run_rss_max}"
    failed=1
  fi

  if (( failed == 0 )); then
    echo "PASS ${case_name} compile_ms=${compile_ms} compile_rss_kb=${compile_rss_kb} run_ms=${run_ms} run_rss_kb=${run_rss_kb}"
    pass_count=$((pass_count + 1))
  else
    fail_count=$((fail_count + 1))
  fi

  rm -f "${bin_file}" "${bin_file}.o" "${compile_metrics}" "${run_metrics}" "${compile_log}" "${run_log}"
done < "${BASELINE_FILE}"

echo "Perf summary: ${pass_count} passed, ${fail_count} failed"
if (( fail_count > 0 )); then
  exit 1
fi
