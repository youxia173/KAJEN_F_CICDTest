#!/usr/bin/env bash
# IKEA OTA version helpers: 0xMMmmPPBB (Major, Minor, Patch, Build).
set -euo pipefail

# Encode decimal components to 0xMMmmPPBB (lowercase hex with 0x prefix).
ikea_version_encode() {
    local major="${1:?major required}"
    local minor="${2:?minor required}"
    local patch="${3:?patch required}"
    local build="${4:?build required}"
    printf '0x%02x%02x%02x%02x' \
        "$((10#${major}))" "$((10#${minor}))" "$((10#${patch}))" "$((10#${build}))"
}

# Parse 0xMMmmPPBB or decimal uint32 into "major minor patch build" on stdout.
ikea_version_decode() {
    local raw="${1:?version required}"
    local value
    if [[ "${raw}" =~ ^0[xX][0-9A-Fa-f]+$ ]]; then
        value=$((raw))
    else
        value=$((10#${raw}))
    fi
    local major=$(( (value >> 24) & 0xFF ))
    local minor=$(( (value >> 16) & 0xFF ))
    local patch=$(( (value >> 8) & 0xFF ))
    local build=$(( value & 0xFF ))
    echo "${major} ${minor} ${patch} ${build}"
}

# Human-readable semver without build (e.g. 1.1.0).
ikea_version_display_string() {
    read -r major minor patch _build < <(ikea_version_decode "$1")
    echo "${major}.${minor}.${patch}"
}

# Previous build in the same Major.Minor.Patch line (build - 1, floored at 0).
ikea_version_prev_build() {
    local raw="${1:?version required}"
    local value
    if [[ "${raw}" =~ ^0[xX][0-9A-Fa-f]+$ ]]; then
        value=$((raw))
    else
        value=$((10#${raw}))
    fi
    local build=$(( value & 0xFF ))
    if (( build == 0 )); then
        printf '0x%08x' "$value"
    else
        printf '0x%08x' "$(( value - 1 ))"
    fi
}

# Parse tag/release version "1.1.0" or "1.1.0.5" into major minor patch build.
ikea_version_parse_tag() {
    local tag="${1#v}"
    local major minor patch build
    IFS='.' read -r major minor patch build <<< "${tag}..."
    major="${major:-0}"
    minor="${minor:-0}"
    patch="${patch:-0}"
    build="${build:-}"
    if [[ -z "${build}" ]]; then
        build=1
    fi
    echo "${major} ${minor} ${patch} ${build}"
}
