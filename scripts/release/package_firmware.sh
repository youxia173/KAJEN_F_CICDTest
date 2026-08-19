#!/usr/bin/env bash
# Rename combined full.s37 for local flash / legacy use.
# GitHub Releases use scripts/release/package_release.sh (unsigned zip).
# Usage:
#   bash scripts/release/package_firmware.sh 0.3.3
#   bash scripts/release/package_firmware.sh 0.3.3 /path/to/out_dir
#
# Source (from Simplicity postbuild):
#   artifact/ZigbeeMatterLightSolution_SixG301M113W-full.s37
# Output:
#   <out>/silabs_MatterAndZigger_SixG301_V0.3.3.s37
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VER="${1:-}"
OUT_DIR="${2:-${ROOT}/release_out}"

if [[ -z "${VER}" ]]; then
    echo "Usage: $(basename "$0") <version> [out_dir]" >&2
    echo "  version example: 0.3.3  (no leading v)" >&2
    exit 1
fi
# Allow accidental "v0.3.3"
VER="${VER#v}"

SRC_FULL="${ROOT}/artifact/ZigbeeMatterLightSolution_SixG301M113W-full.s37"
DST_NAME="silabs_MatterAndZigger_SixG301_V${VER}.s37"

if [[ ! -f "${SRC_FULL}" ]]; then
    echo "ERROR: missing build output: ${SRC_FULL}" >&2
    echo "Build firmware first (local: bash scripts/ci_local.sh --with-build), then re-run." >&2
    exit 1
fi

mkdir -p "${OUT_DIR}"
# Only the current versioned firmware — do not copy historical renamed copies.
rm -f "${OUT_DIR}/${DST_NAME}"
cp -a "${SRC_FULL}" "${OUT_DIR}/${DST_NAME}"

echo "Packaged: ${SRC_FULL}"
echo "      ->  ${OUT_DIR}/${DST_NAME}"
ls -la "${OUT_DIR}/${DST_NAME}"
