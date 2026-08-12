#!/bin/bash
# Install Silicon Labs packages via slt (one at a time for clear CI logs).
set -euo pipefail

install_one() {
    local pkg="$1"
    local attempt=1
    local max=3
    echo "========================================"
    echo "slt install: ${pkg}"
    echo "========================================"
    while [ "${attempt}" -le "${max}" ]; do
        if slt --non-interactive install "${pkg}"; then
            return 0
        fi
        echo "WARN: slt install failed (${attempt}/${max}): ${pkg}"
        attempt=$((attempt + 1))
        sleep 15
    done
    echo "ERROR: slt install failed after ${max} attempts: ${pkg}" >&2
    exit 1
}

if [ "${INSTALL_SLT_PACKAGES:-1}" != "1" ]; then
    echo "Skip slt package install (INSTALL_SLT_PACKAGES=${INSTALL_SLT_PACKAGES})"
    exit 0
fi

install_one conan
install_one "cmake/${CMAKE_VERSION:?}"
install_one "ninja/${NINJA_VERSION:?}"
install_one "commander/${COMMANDER_VERSION:?}"
install_one "gcc-arm-none-eabi/${GCC_ARM_VERSION:?}"
install_one "simplicity-sdk/${SIMPLICITY_SDK_VERSION:?}"
install_one "matter_extension/${MATTER_EXTENSION_VERSION:?}"

# SLC / ZAP only needed for `generate`, not for `build` in CI.
if [ "${INSTALL_SLC_TOOLS:-0}" = "1" ]; then
    install_one "java21/${JAVA21_VERSION:?}"
    install_one "zap/${ZAP_VERSION:?}"
    install_one "slc-cli/${SLC_CLI_VERSION:?}"
else
    echo "Skip java/zap/slc-cli (INSTALL_SLC_TOOLS=${INSTALL_SLC_TOOLS:-0})"
fi

mkdir -p "${HOME}/.silabs/slt/bin"
ln -sfn "$(slt where cmake)/bin/cmake" "${HOME}/.silabs/slt/bin/cmake"
ln -sfn "$(slt where ninja)/ninja" "${HOME}/.silabs/slt/bin/ninja"
ln -sfn "$(slt where commander)/commander" "${HOME}/.silabs/slt/bin/commander"
ln -sfn "$(slt where gcc-arm-none-eabi)/bin/arm-none-eabi-gcc" \
    "${HOME}/.silabs/slt/bin/arm-none-eabi-gcc"

if [ "${INSTALL_SLC_TOOLS:-0}" = "1" ]; then
    ln -sfn "$(slt where java21)/jre/bin/java" "${HOME}/.silabs/slt/bin/java"
    ln -sfn "$(slt where zap)/zap-cli" "${HOME}/.silabs/slt/bin/zap-cli"
    ln -sfn "$(slt where zap)/zap" "${HOME}/.silabs/slt/bin/zap"
    printf '#!/bin/sh\nexec "%s/slc" "$@"\n' "$(slt where slc-cli)" \
        > "${HOME}/.silabs/slt/bin/slc"
    chmod +x "${HOME}/.silabs/slt/bin/slc"
    java -version
    slc --help >/dev/null
fi

rm -rf \
    "${HOME}/.silabs/slt/installs/archive/"*.zip \
    "${HOME}/.silabs/slt/installs/archive/"*.tar.* \
    "${HOME}/.silabs/slt/installs/conan/p/"*/d/ || true

echo "slt packages installed OK"
