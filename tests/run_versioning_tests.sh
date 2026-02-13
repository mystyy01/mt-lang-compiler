#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_PATH="${ROOT_DIR}/dist/mtc"
VERSION_FILE="${ROOT_DIR}/VERSION"
CHANGELOG_FILE="${ROOT_DIR}/CHANGELOG.md"
RELEASE_FILE="${ROOT_DIR}/RELEASE.md"
PLATFORMS_FILE="${ROOT_DIR}/PLATFORMS.md"
ABI_FILE="${ROOT_DIR}/RUNTIME_ABI.md"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "missing compiler binary: ${BIN_PATH}" >&2
  exit 1
fi

for required_file in "${VERSION_FILE}" "${CHANGELOG_FILE}" "${RELEASE_FILE}" "${PLATFORMS_FILE}" "${ABI_FILE}"; do
  if [[ ! -f "${required_file}" ]]; then
    echo "FAIL versioning (missing required file: ${required_file})"
    exit 1
  fi
done

version="$(tr -d '[:space:]' < "${VERSION_FILE}")"
if [[ -z "${version}" ]]; then
  echo "FAIL versioning (empty VERSION file)"
  exit 1
fi

reported="$("${BIN_PATH}" --version | tr -d '\r')"
expected="mtc ${version}"
if [[ "${reported}" != "${expected}" ]]; then
  echo "FAIL versioning (compiler version mismatch)"
  echo "Expected: ${expected}"
  echo "Actual:   ${reported}"
  exit 1
fi

if ! rg -q "^## \\[${version}\\]" "${CHANGELOG_FILE}"; then
  echo "FAIL versioning (CHANGELOG missing version heading ${version})"
  exit 1
fi

if ! rg -q "make check" "${RELEASE_FILE}"; then
  echo "FAIL versioning (RELEASE process missing quality gate step)"
  exit 1
fi

echo "PASS versioning"
