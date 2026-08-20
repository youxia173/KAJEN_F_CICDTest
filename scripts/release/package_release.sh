#!/usr/bin/env bash
# Package IKEA submission unsigned release zip.
#
# Usage:
#   bash scripts/release/package_release.sh 1.1.0
#   bash scripts/release/package_release.sh 1.1.0 /path/to/out_dir
#
# Output:
#   <out>/{IKEA_PROJECT_ID}-{M.m.p}-unsigned.zip
#     bootloader.s37
#     firmware.s37
#     firmware.ota
#     config.json
#   <out>/RELEASE_NOTES.md
#
# Requires Simplicity Commander on PATH (or COMMANDER=…) for firmware.ota.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VER="${1:-}"
OUT_DIR="${2:-${ROOT}/release_out}"

if [[ -z "${VER}" ]]; then
    echo "Usage: $(basename "$0") <version> [out_dir]" >&2
    echo "  version example: 1.1.0 or 1.1.0.1 (no leading v)" >&2
    exit 1
fi
VER="${VER#v}"

mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

# shellcheck source=/dev/null
source "${ROOT}/scripts/release/project.env"
# shellcheck source=ikea_version.sh
source "${ROOT}/scripts/release/ikea_version.sh"

MATTER_CFG="${ROOT}/ZigbeeMatterLight_113W/config/sl_matter_config.h"
SRC_BOOT="${ROOT}/Matter-Bootloader_113W/artifact/Matter-Bootloader_113W.s37"
SRC_APP="${ROOT}/ZigbeeMatterLight_113W/artifact/ZigbeeMatterLight_113W.s37"

read_fw_version() {
    local line
    line="$(grep -m1 '^#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION ' "${MATTER_CFG}" || true)"
    if [[ -z "${line}" ]]; then
        echo "ERROR: cannot read CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION from ${MATTER_CFG}" >&2
        exit 1
    fi
    echo "${line##* }" | tr -d ' '
}

read_fw_version_string() {
    local line
    line="$(grep -m1 '^#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING ' "${MATTER_CFG}" || true)"
    if [[ -z "${line}" ]]; then
        echo "ERROR: cannot read CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING from ${MATTER_CFG}" >&2
        exit 1
    fi
    echo "${line##* }" | tr -d '" '
}

FW_VERSION_RAW="$(read_fw_version)"
FW_VERSION_STRING="$(read_fw_version_string)"
read -r TAG_MAJOR TAG_MINOR TAG_PATCH TAG_BUILD < <(ikea_version_parse_tag "${VER}")
TAG_DISPLAY="${TAG_MAJOR}.${TAG_MINOR}.${TAG_PATCH}"

if [[ "${FW_VERSION_STRING}" != "${TAG_DISPLAY}" ]]; then
    echo "ERROR: tag version ${TAG_DISPLAY} does not match firmware string ${FW_VERSION_STRING}" >&2
    echo "  Bump sl_matter_config.h before packaging." >&2
    exit 1
fi

read -r FW_MAJOR FW_MINOR FW_PATCH FW_BUILD < <(ikea_version_decode "${FW_VERSION_RAW}")
if [[ "${FW_MAJOR}" != "${TAG_MAJOR}" || "${FW_MINOR}" != "${TAG_MINOR}" || "${FW_PATCH}" != "${TAG_PATCH}" ]]; then
    echo "ERROR: firmware version ${FW_VERSION_RAW} (${FW_MAJOR}.${FW_MINOR}.${FW_PATCH}.${FW_BUILD})" >&2
    echo "  does not match tag ${TAG_DISPLAY}.${TAG_BUILD}" >&2
    exit 1
fi

VERSION_HEX="$(ikea_version_encode "${FW_MAJOR}" "${FW_MINOR}" "${FW_PATCH}" "${FW_BUILD}")"
if [[ "${VERSION_HEX}" != "$(printf '0x%08x' $((FW_VERSION_RAW)))" ]]; then
    echo "WARN: normalizing firmware version ${FW_VERSION_RAW} -> ${VERSION_HEX}" >&2
fi

if [[ -n "${IKEA_OTA_MAX_VERSION}" ]]; then
    MAX_VERSION_HEX="${IKEA_OTA_MAX_VERSION}"
else
    MAX_VERSION_HEX="$(ikea_version_prev_build "${VERSION_HEX}")"
fi
MIN_VERSION_HEX="${IKEA_OTA_MIN_VERSION}"

ZIP_NAME="${IKEA_PROJECT_ID}-${TAG_DISPLAY}-unsigned.zip"
STAGE="${OUT_DIR}/.pack_staging_${TAG_DISPLAY}"
rm -rf "${STAGE}"
mkdir -p "${STAGE}"

missing=0
for f in "${SRC_BOOT}" "${SRC_APP}"; do
    if [[ ! -f "${f}" ]]; then
        echo "ERROR: missing build output: ${f}" >&2
        missing=1
    fi
done
if [[ "${missing}" -ne 0 ]]; then
    echo "Build firmware first (CI: firmware-build job / local: docker run ... build)." >&2
    exit 1
fi

cp -a "${SRC_BOOT}" "${STAGE}/bootloader.s37"
cp -a "${SRC_APP}" "${STAGE}/firmware.s37"

bash "${ROOT}/scripts/release/create_ota.sh" \
    "${SRC_APP}" \
    "${STAGE}" \
    "${FW_VERSION_STRING}" \
    "${VERSION_HEX}"

# Intermediate .gbl stays out of the IKEA submission zip.
rm -f "${STAGE}/firmware.gbl"

python3 - "${STAGE}/config.json" <<PY
import json
import sys

payload = {
    "productId": "${IKEA_PRODUCT_ID}",
    "version": "${VERSION_HEX}",
    "minVersion": "${MIN_VERSION_HEX}",
    "maxVersion": "${MAX_VERSION_HEX}",
}
with open(sys.argv[1], "w", encoding="utf-8") as fh:
    json.dump(payload, fh, indent=2)
    fh.write("\n")
PY

rm -f "${OUT_DIR}/${ZIP_NAME}"
python3 - "${STAGE}" "${OUT_DIR}/${ZIP_NAME}" <<'PY'
import sys
import zipfile
from pathlib import Path

stage = Path(sys.argv[1])
zip_path = Path(sys.argv[2])
with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
    for path in sorted(stage.iterdir()):
        if path.is_file():
            zf.write(path, arcname=path.name)
PY

cat > "${OUT_DIR}/RELEASE_NOTES.md" <<EOF
# KAJEN_F / SixG301 release ${TAG_DISPLAY} (build ${FW_BUILD})

- Package: ${ZIP_NAME}
- Project ID: ${IKEA_PROJECT_ID}
- Matter productId (config.json): ${IKEA_PRODUCT_ID}
- OTA file VID/PID: ${OTA_VENDOR_ID:-0xFFF1} / ${OTA_PRODUCT_ID:-0x8005}
- OTA version: ${VERSION_HEX} (${TAG_DISPLAY}.${FW_BUILD})
- OTA window: min ${MIN_VERSION_HEX}, max ${MAX_VERSION_HEX}
- Commit: ${GITHUB_SHA:-local}

## Files (inside zip, IKEA submission layout)

| File | Purpose |
|---|---|
| \`bootloader.s37\` | Bootloader |
| \`firmware.s37\` | Application firmware |
| \`firmware.ota\` | Matter OTA image (gbl4 + LZMA) |
| \`config.json\` | OTA metadata (productId, version, min/max) |

## Local flash (repo artifact, not in zip)

\`\`\`bash
./flash.sh -i artifact/ZigbeeMatterLightSolution_SixG301M113W-full.s37
\`\`\`
EOF

rm -rf "${STAGE}"

echo "Packaged release zip:"
echo "  ${OUT_DIR}/${ZIP_NAME}"
echo "config.json:"
python3 - "${OUT_DIR}/${ZIP_NAME}" <<'PY'
import json
import sys
import zipfile

with zipfile.ZipFile(sys.argv[1]) as zf:
    print(json.dumps(json.loads(zf.read("config.json")), indent=4))
    print("Contents:")
    for info in zf.infolist():
        print(f"  {info.file_size:>10}  {info.filename}")
PY
