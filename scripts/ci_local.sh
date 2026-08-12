#!/usr/bin/env bash
# Local mirror of GitHub Actions CI (IKEA NonFuncReq: same steps as pipeline).
# Usage:
#   bash scripts/ci_local.sh              # lint + cppcheck
#   bash scripts/ci_local.sh --strict     # fail on lint findings
#   bash scripts/ci_local.sh --with-unittest
#   bash scripts/ci_local.sh --with-build # firmware cmake build (needs SiSDK env)
#   bash scripts/ci_local.sh --build-only # firmware only (no lint/cppcheck)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

STRICT=0
WITH_UNITTEST=0
WITH_BUILD=0
BUILD_ONLY=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Run the same CI stages locally (IKEA: pipeline logic lives in scripts).

Options:
  --strict          Fail the script if cpplint reports issues
  --with-unittest   Also run scripts/unittest/runner.sh (needs kt_components)
  --with-build      Also build firmware via cmake preset
  --build-only      Firmware cmake build only (skip lint/cppcheck)
  -h, --help        Show help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --strict) STRICT=1 ;;
        --with-unittest) WITH_UNITTEST=1 ;;
        --with-build) WITH_BUILD=1 ;;
        --build-only) BUILD_ONLY=1; WITH_BUILD=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
    shift
done

silabs_toolchain_ok() {
    if command -v slt >/dev/null 2>&1; then
        if slt where ninja >/dev/null 2>&1 && slt where gcc-arm-none-eabi >/dev/null 2>&1; then
            return 0
        fi
    fi
    local ninja="${HOME}/.silabs/slt/installs/conan/p/ninja1b9fed093d653/p/ninja"
    local gcc="${HOME}/.silabs/slt/installs/conan/p/gcc-a442105b5c2637/p/bin/arm-none-eabi-gcc"
    if [[ -x "${ninja}" && -x "${gcc}" ]]; then
        return 0
    fi
    return 1
}

build_firmware() {
    echo "==> [extra] firmware build"
    CMAKE_DIR="${ROOT}/ZigbeeMatterLightSolution_SixG301M113W_cmake"
    if [[ ! -d "${CMAKE_DIR}" ]]; then
        echo "ERROR: missing ${CMAKE_DIR}" >&2
        exit 1
    fi
    if ! silabs_toolchain_ok; then
        cat >&2 <<EOF
ERROR: Silicon Labs toolchain not found (slt / ninja / arm-none-eabi-gcc).

This firmware cannot be built on a plain GitHub-hosted ubuntu-latest runner.
It needs Simplicity Studio / slt on a self-hosted Linux runner (or Docker image).

Repo variables:
  ENABLE_FIRMWARE_BUILD=true
  SILABS_RUNNER=<self-hosted runner label that has SiSDK>
EOF
        if [[ "${CI_ALLOW_SKIP_FIRMWARE:-}" == "true" ]]; then
            echo "WARN: skipping firmware build (CI_ALLOW_SKIP_FIRMWARE=true)"
            return 0
        fi
        exit 1
    fi
    if [[ -e "${CMAKE_DIR}/build" && ! -d "${CMAKE_DIR}/build" ]]; then
        echo "WARN: ${CMAKE_DIR}/build exists but is not a directory; removing"
        rm -f "${CMAKE_DIR}/build"
    fi
    mkdir -p "${CMAKE_DIR}/build"
    (cd "${CMAKE_DIR}" && cmake --preset project && cmake --build --preset default_config)
    echo "Build artifacts under artifact/ (after packaging step if configured)"
}

if [[ "${BUILD_ONLY}" -eq 1 ]]; then
    build_firmware
    echo "==> firmware build stage finished"
    exit 0
fi

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
    build_firmware
fi

echo "==> CI local stages finished OK"
echo "    Reports: ${ROOT}/code_quality_report/"
