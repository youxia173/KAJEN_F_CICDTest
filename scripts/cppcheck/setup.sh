#!/bin/bash

set -euo pipefail

SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
THIRD_PARTY_DIR="${SCRIPT_PATH}/../../third_party"
CPPCHECK_DIR="${THIRD_PARTY_DIR}/cppcheck"
CPPCHECK_REPO="${CPPCHECK_REPO:-https://github.com/danmar/cppcheck.git}"
CPPCHECK_REF="${CPPCHECK_REF:-2.17.x}"

clone_cppcheck_with_retry() {
    local dest="$1"
    local attempts=3
    local i

    for ((i=1; i<=attempts; i++)); do
        echo "cppcheck clone attempt ${i}/${attempts}"
        if git -c http.version=HTTP/1.1 clone --depth 1 -b "${CPPCHECK_REF}" "${CPPCHECK_REPO}" "${dest}"; then
            return 0
        fi
        rm -rf "${dest}"
        sleep 2
    done

    return 1
}

download_cppcheck_tarball() {
    local dest="$1"
    local tar_url="https://codeload.github.com/danmar/cppcheck/tar.gz/refs/heads/${CPPCHECK_REF}"

    echo "fallback to tarball download: ${tar_url}"
    mkdir -p "${dest}"
    curl -fL --retry 5 --retry-delay 2 --retry-connrefused "${tar_url}" | tar -xz --strip-components=1 -C "${dest}"
}

build_cppcheck() {
    cd "${CPPCHECK_DIR}"
    make -j"$(nproc)"
    cd - >/dev/null
}

mkdir -p "${THIRD_PARTY_DIR}"

if [ ! -d "${CPPCHECK_DIR}" ] || [ ! -f "${CPPCHECK_DIR}/Makefile" ]; then
    echo "cppcheck install"
    TMP_DIR="${THIRD_PARTY_DIR}/cppcheck.tmp.$$"

    rm -rf "${TMP_DIR}"
    if ! clone_cppcheck_with_retry "${TMP_DIR}"; then
        download_cppcheck_tarball "${TMP_DIR}"
    fi

    rm -rf "${CPPCHECK_DIR}"
    mv "${TMP_DIR}" "${CPPCHECK_DIR}"
    build_cppcheck
else
    echo "cppcheck already installed"
fi

