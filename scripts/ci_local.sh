#!/usr/bin/env bash
# Local mirror of GitHub Actions CI (IKEA NonFuncReq: same steps as pipeline).
# Usage:
#   bash scripts/ci_local.sh              # lint + cppcheck
#   bash scripts/ci_local.sh --strict     # fail on lint findings
#   bash scripts/ci_local.sh --with-unittest
#   bash scripts/ci_local.sh --with-build # firmware cmake build (needs SiSDK env)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

STRICT=0
WITH_UNITTEST=0
WITH_BUILD=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Run the same CI stages locally (IKEA: pipeline logic lives in scripts).

Options:
  --strict          Fail the script if cpplint reports issues
  --with-unittest   Also run scripts/unittest/runner.sh (needs kt_components)
  --with-build      Also build firmware via cmake preset
  -h, --help        Show help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --strict) STRICT=1 ;;
        --with-unittest) WITH_UNITTEST=1 ;;
        --with-build) WITH_BUILD=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
    shift
done

mkdir -p "${ROOT}/code_quality_report"

echo "==> [1/3] Setup cpplint"
bash "${ROOT}/scripts/linter/setup.sh"

echo "==> [2/3] cpplint (app sources)"
set +e
bash "${ROOT}/scripts/linter/linter.sh" -p
LINT_RC=$?
set -e
if [[ "${STRICT}" -eq 1 && "${LINT_RC}" -ne 0 ]]; then
    echo "ERROR: cpplint failed (strict mode)" >&2
    exit "${LINT_RC}"
fi
if [[ "${LINT_RC}" -ne 0 ]]; then
    echo "WARN: cpplint reported findings (non-strict); see code_quality_report/cpplint/"
fi

echo "==> [3/3] cppcheck"
if [[ ! -x "${ROOT}/third_party/cppcheck/cppcheck" ]]; then
    bash "${ROOT}/scripts/cppcheck/setup.sh"
fi
bash "${ROOT}/scripts/cppcheck/runner.sh"

if [[ "${WITH_UNITTEST}" -eq 1 ]]; then
    echo "==> [extra] unittest"
    if [[ ! -d "${ROOT}/kt_components" && ! -d "${ROOT}/unit_test" ]]; then
        echo "WARN: unittest scaffolding (kt_components/unit_test) not present — skip"
    else
        bash "${ROOT}/scripts/unittest/setup.sh" || true
        bash "${ROOT}/scripts/unittest/runner.sh"
    fi
fi

if [[ "${WITH_BUILD}" -eq 1 ]]; then
    echo "==> [extra] firmware build"
    CMAKE_DIR="${ROOT}/ZigbeeMatterLightSolution_SixG301M113W_cmake"
    if [[ ! -d "${CMAKE_DIR}" ]]; then
        echo "ERROR: missing ${CMAKE_DIR}" >&2
        exit 1
    fi
    # Be defensive: in a clean CI checkout, the binaryDir might not exist yet.
    # If something created a non-directory file at `${CMAKE_DIR}/build`, cmake will fail.
    if [[ -e "${CMAKE_DIR}/build" && ! -d "${CMAKE_DIR}/build" ]]; then
        echo "WARN: ${CMAKE_DIR}/build exists but is not a directory; removing"
        rm -f "${CMAKE_DIR}/build"
    fi
    mkdir -p "${CMAKE_DIR}/build"

    # Configure first, then build (more reliable across different CMake versions/CI states).
    (cd "${CMAKE_DIR}" && cmake --preset project && cmake --build --preset default_config)
    echo "Build artifacts under artifact/ (after packaging step if configured)"
fi

echo "==> CI local stages finished OK"
echo "    Reports: ${ROOT}/code_quality_report/"
