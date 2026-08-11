#!/usr/bin/env bash
# Bundle quality reports (used by local packaging; CI uploads per-job artifacts).
set -euo pipefail

SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
ROOT="${SCRIPT_PATH}/../.."
REPORT_PATH="${ROOT}/code_quality_report"

mkdir -p "${REPORT_PATH}"

bash "${SCRIPT_PATH}/../linter/linter.sh" -p
bash "${SCRIPT_PATH}/../cppcheck/runner.sh"

tar -zcvf "${ROOT}/code_quality_report.tar.gz" -C "${ROOT}" code_quality_report
echo "Wrote ${ROOT}/code_quality_report.tar.gz"
