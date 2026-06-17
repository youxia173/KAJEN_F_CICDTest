#!/bin/bash

SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
REPO_ROOT="${SCRIPT_PATH}/../.."
PROJECT_DIR="${CPPCHECK_PROJECT_DIR:-${REPO_ROOT}/ZigbeeMatterLight_113W}"
PROJECT_NAME="$(basename "${PROJECT_DIR}")"

REPORT_PATH="${REPO_ROOT}/code_quality_report"
CPPCHECK_REPORT_PATH="${REPORT_PATH}/cppcheck"
CPPCHECK_BIN="${REPO_ROOT}/third_party/cppcheck/cppcheck"
CPPCHECK_HTMLREPORT="${REPO_ROOT}/third_party/cppcheck/htmlreport/cppcheck-htmlreport"

if [ ! -x "${CPPCHECK_BIN}" ]; then
    echo "cppcheck not found; run: bash scripts/cppcheck/setup.sh"
    exit 1
fi

if [ ! -d "${PROJECT_DIR}/src" ]; then
    echo "project src not found: ${PROJECT_DIR}/src"
    exit 1
fi

mkdir -p "${CPPCHECK_REPORT_PATH}"
rm -rf "${CPPCHECK_REPORT_PATH:?}"/*

echo "cppcheck scan: ${PROJECT_NAME} (src + include)"

"${CPPCHECK_BIN}" -j"$(nproc)" \
    --enable=warning,style,performance,portability \
    --std=c++17 \
    --inline-suppr \
    --xml \
    '-D__ALIGNED(x)=' \
    '-DCHIP_HAVE_CONFIG_H=1' \
    '-D_SILICON_LABS_32B_SERIES_2=1' \
    '-DCHIP_ERROR_FORMAT=s' \
    '-DChipLogFormatMEI="0x%04X_%04X"' \
    '-DChipLogValueMEI(x)=(x)' \
    '-DSILABS_LOG(...)' \
    -I "${PROJECT_DIR}/include" \
    -i "${PROJECT_DIR}/matter_2.8.0" \
    -i "${PROJECT_DIR}/simplicity_sdk_2025.12.1" \
    -i "${PROJECT_DIR}/cmake_gcc" \
    -i "${PROJECT_DIR}/autogen" \
    --suppress='*:*/test/*' \
    --suppress='*:*_cli.c' \
    --suppress='unknownMacro' \
    --suppress='constParameterCallback:*LightingManager.cpp' \
    "${PROJECT_DIR}/src" \
    "${PROJECT_DIR}/include" \
    &> "${CPPCHECK_REPORT_PATH}/cppcheck_report.xml"

"${CPPCHECK_HTMLREPORT}" \
    --file="${CPPCHECK_REPORT_PATH}/cppcheck_report.xml" \
    --title="${PROJECT_NAME}" \
    --report-dir="${CPPCHECK_REPORT_PATH}" \
    --source-dir="${PROJECT_DIR}"

echo "Report: ${CPPCHECK_REPORT_PATH}/index.html"
echo "XML:    ${CPPCHECK_REPORT_PATH}/cppcheck_report.xml"
