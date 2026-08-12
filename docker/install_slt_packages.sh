#!/bin/bash
# Install Silicon Labs packages via slt (retries + clear CI logs).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SLT_MAX_ATTEMPTS="${SLT_MAX_ATTEMPTS:-5}"
SLT_RETRY_SLEEP_SEC="${SLT_RETRY_SLEEP_SEC:-20}"

install_one() {
    local pkg="$1"
    local attempt=1
    echo "========================================"
    echo "slt install: ${pkg}"
    echo "========================================"
    while [ "${attempt}" -le "${SLT_MAX_ATTEMPTS}" ]; do
        if slt --non-interactive install "${pkg}"; then
            return 0
        fi
        echo "WARN: slt install failed (${attempt}/${SLT_MAX_ATTEMPTS}): ${pkg}"

        # If a previous download got corrupted, SLT/Conan may keep the bad
        # `conan_package.tgz` under `${HOME}/.silabs/slt/installs/conan/p/*/d/`.
        # Remove it to force re-download on next attempt.
        rm -f "${HOME}/.silabs/slt/installs/conan/p/"*/d/conan_package.tgz 2>/dev/null || true

        attempt=$((attempt + 1))
        sleep "${SLT_RETRY_SLEEP_SEC}"
    done
    echo "ERROR: slt install failed after ${SLT_MAX_ATTEMPTS} attempts: ${pkg}" >&2
    return 1
}

slt_where_or_fail() {
    local pkg="$1"
    local path=""
    path="$(slt where "${pkg}" 2>/dev/null || true)"
    if [ -z "${path}" ] || [ ! -e "${path}" ]; then
        echo "ERROR: slt where '${pkg}' returned no path" >&2
        return 1
    fi
    echo "${path}"
}

install_matter_extension() {
    # Only install via "~" because available `matter_extension/<x.y.z>` versions
    # are not guaranteed to match `simplicity-sdk/<release>` versions in CI.
    # Using "~" lets SLT resolve a compatible version (e.g. the 2.9.1 in GHCR logs).
    install_one "matter_extension/~"
}

if [ "${INSTALL_SLT_PACKAGES:-1}" != "1" ]; then
    echo "Skip slt package install (INSTALL_SLT_PACKAGES=${INSTALL_SLT_PACKAGES})"
    exit 0
fi

echo "slt version:"
slt --version || true

RECIPE="${SCRIPT_DIR}/pkg.ci.slt"
if [ "${INSTALL_SLC_TOOLS:-0}" = "1" ]; then
    RECIPE="${SCRIPT_DIR}/pkg.full.slt"
fi

echo "========================================"
echo "slt install: conan (engine)"
echo "========================================"
install_one conan

configure_conan_remote() {
    local url="${CONAN_REMOTE_URL:-}"
    if [ -z "${url}" ]; then
        return 0
    fi
    local remotes_json="${HOME}/.silabs/slt/installs/conan/remotes.json"
    if [ ! -f "${remotes_json}" ]; then
        echo "WARN: conan remotes.json not found: ${remotes_json}" >&2
        return 0
    fi
    echo "Config conan remote url -> ${url}"
    # Update the first remote entry (named public-conan-silabs-net) to alternate mirror.
    # jq is installed in the Dockerfile apt deps.
    jq --arg url "${url}" '.remotes[0].url = $url' "${remotes_json}" > "${remotes_json}.tmp"
    mv -f "${remotes_json}.tmp" "${remotes_json}"
}

configure_conan_remote

if [ -f "${RECIPE}" ]; then
    echo "========================================"
    echo "slt install -f ${RECIPE}"
    echo "========================================"
    if slt --non-interactive install -f "${RECIPE}"; then
        echo "slt recipe install OK"
    else
        echo "WARN: slt recipe failed; falling back to per-package install..." >&2
        install_one "cmake/${CMAKE_VERSION:?}"
        install_one "ninja/${NINJA_VERSION:?}"
        install_one "commander/${COMMANDER_VERSION:?}"
        install_one "gcc-arm-none-eabi/${GCC_ARM_VERSION:?}"
        install_one "simplicity-sdk/${SIMPLICITY_SDK_VERSION:?}"
        install_matter_extension
        if [ "${INSTALL_SLC_TOOLS:-0}" = "1" ]; then
            install_one "java21/${JAVA21_VERSION:?}"
            install_one "zap/${ZAP_VERSION:?}"
            install_one "slc-cli/${SLC_CLI_VERSION:?}"
        fi
    fi
else
    echo "WARN: recipe ${RECIPE} missing; per-package install" >&2
    install_one "cmake/${CMAKE_VERSION:?}"
    install_one "ninja/${NINJA_VERSION:?}"
    install_one "commander/${COMMANDER_VERSION:?}"
    install_one "gcc-arm-none-eabi/${GCC_ARM_VERSION:?}"
    install_one "simplicity-sdk/${SIMPLICITY_SDK_VERSION:?}"
    install_matter_extension
    if [ "${INSTALL_SLC_TOOLS:-0}" = "1" ]; then
        install_one "java21/${JAVA21_VERSION:?}"
        install_one "zap/${ZAP_VERSION:?}"
        install_one "slc-cli/${SLC_CLI_VERSION:?}"
    fi
fi

mkdir -p "${HOME}/.silabs/slt/bin"
ln -sfn "$(slt_where_or_fail cmake)/bin/cmake" "${HOME}/.silabs/slt/bin/cmake"
ln -sfn "$(slt_where_or_fail ninja)/ninja" "${HOME}/.silabs/slt/bin/ninja"
ln -sfn "$(slt_where_or_fail commander)/commander" "${HOME}/.silabs/slt/bin/commander"
ln -sfn "$(slt_where_or_fail gcc-arm-none-eabi)/bin/arm-none-eabi-gcc" \
    "${HOME}/.silabs/slt/bin/arm-none-eabi-gcc"

if [ "${INSTALL_SLC_TOOLS:-0}" = "1" ]; then
    ln -sfn "$(slt_where_or_fail java21)/jre/bin/java" "${HOME}/.silabs/slt/bin/java"
    ln -sfn "$(slt_where_or_fail zap)/zap-cli" "${HOME}/.silabs/slt/bin/zap-cli"
    ln -sfn "$(slt_where_or_fail zap)/zap" "${HOME}/.silabs/slt/bin/zap"
    printf '#!/bin/sh\nexec "%s/slc" "$@"\n' "$(slt_where_or_fail slc-cli)" \
        > "${HOME}/.silabs/slt/bin/slc"
    chmod +x "${HOME}/.silabs/slt/bin/slc"
    java -version
    slc --help >/dev/null
fi

rm -rf \
    "${HOME}/.silabs/slt/installs/archive/"*.zip \
    "${HOME}/.silabs/slt/installs/archive/"*.tar.* \
    "${HOME}/.silabs/slt/installs/conan/p/"*/d/ 2>/dev/null || true

echo "slt packages installed OK"
slt where simplicity-sdk || true
slt where matter_extension || true
