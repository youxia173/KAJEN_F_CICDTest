#!/bin/bash
# Install Silicon Labs packages via slt (recipe file + clear CI logs).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "${INSTALL_SLT_PACKAGES:-1}" != "1" ]; then
    echo "Skip slt package install (INSTALL_SLT_PACKAGES=${INSTALL_SLT_PACKAGES})"
    exit 0
fi

RECIPE="${SCRIPT_DIR}/pkg.slt.ci"
if [ "${INSTALL_SLC_TOOLS:-0}" = "1" ]; then
    RECIPE="${SCRIPT_DIR}/pkg.slt.full"
fi

echo "slt version:"
slt --version || true

echo "========================================"
echo "slt install: conan (engine)"
echo "========================================"
if ! slt --non-interactive install conan; then
    echo "WARN: slt install conan failed; trying slt install without recipe..."
fi

echo "========================================"
echo "slt install -f ${RECIPE}"
echo "========================================"
if ! slt --non-interactive install -f "${RECIPE}"; then
    echo "ERROR: slt recipe install failed. Falling back to per-package install..." >&2
    install_one() {
        local pkg="$1"
        echo "slt install: ${pkg}"
        slt --non-interactive install "${pkg}" || {
            echo "ERROR: failed: ${pkg}" >&2
            exit 1
        }
    }
    install_one "cmake/${CMAKE_VERSION:?}"
    install_one "ninja/${NINJA_VERSION:?}"
    install_one "commander/${COMMANDER_VERSION:?}"
    install_one "gcc-arm-none-eabi/${GCC_ARM_VERSION:?}"
    install_one "simplicity-sdk/${SIMPLICITY_SDK_VERSION:?}"
    # matter_extension version follows SiSDK release id (not Matter 2.8.x component id)
    install_one "matter_extension/${SIMPLICITY_SDK_VERSION:?}"
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
slt where simplicity-sdk || true
slt where matter_extension || true
