#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
LSP_BIN="${ROOT_DIR}/dist/mtc_lsp"
DOC_PATH="${ROOT_DIR}/tests/e2e/simple_import_alias.mtc"
TARGET_PATH="${ROOT_DIR}/tests/e2e/simplemath.mtc"

if [[ ! -x "${LSP_BIN}" ]]; then
  echo "missing lsp binary: ${LSP_BIN}" >&2
  exit 1
fi

if [[ ! -f "${DOC_PATH}" ]]; then
  echo "missing test file: ${DOC_PATH}" >&2
  exit 1
fi

send_lsp_msg() {
  local json="$1"
  printf 'Content-Length: %d\r\n\r\n%s' "${#json}" "${json}"
}

root_uri="file://${ROOT_DIR}"
doc_uri="file://${DOC_PATH}"
target_uri="file://${TARGET_PATH}"
open_text="use simplemath as sm\nprint(sm.twice(6))\n"

msg_init="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"${root_uri}\",\"capabilities\":{}}}"
msg_open="{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"${doc_uri}\",\"languageId\":\"mtc\",\"version\":1,\"text\":\"${open_text}\"}}}"
msg_def_mod="{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":\"${doc_uri}\"},\"position\":{\"line\":0,\"character\":5}}}"
msg_def_alias="{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":\"${doc_uri}\"},\"position\":{\"line\":0,\"character\":18}}}"
msg_def_use="{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":\"${doc_uri}\"},\"position\":{\"line\":1,\"character\":7}}}"
msg_shutdown="{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"shutdown\",\"params\":null}"
msg_exit="{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}"

output_file="$(mktemp /tmp/mtc_lsp_smoke.XXXXXX.out)"
{
  send_lsp_msg "${msg_init}"
  send_lsp_msg "${msg_open}"
  send_lsp_msg "${msg_def_mod}"
  send_lsp_msg "${msg_def_alias}"
  send_lsp_msg "${msg_def_use}"
  send_lsp_msg "${msg_shutdown}"
  send_lsp_msg "${msg_exit}"
} | "${LSP_BIN}" > "${output_file}"

if ! grep -q "\"id\":1,\"result\"" "${output_file}"; then
  echo "FAIL lsp initialize"
  cat "${output_file}"
  rm -f "${output_file}"
  exit 1
fi

for req_id in 2 3 4; do
  if ! grep -q "\"id\":${req_id},\"result\":\\[{\"uri\":\"${target_uri}\"" "${output_file}"; then
    echo "FAIL lsp definition request ${req_id}"
    cat "${output_file}"
    rm -f "${output_file}"
    exit 1
  fi
done

rm -f "${output_file}"
echo "PASS lsp smoke"
