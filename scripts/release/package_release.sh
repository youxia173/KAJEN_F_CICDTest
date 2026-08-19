#!/usr/bin/env bash
# Package unsigned release zip with separate bootloader and application flash files.
#
# Usage:
#   bash scripts/release/package_release.sh 0.3.6
#   bash scripts/release/package_release.sh 0.3.6 /path/to/out_dir
#
# Output:
#   <out>/{IKEA_PROJECT_ID}-{VERSION}-unsigned.zip
#     Matter-Bootloader_113W.s37
#     ZigbeeMatterLight_113W.s37
#   <out>/RELEASE_NOTES.md
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VER="${1:-}"
OUT_DIR="${2:-${ROOT}/release_out}"

if [[ -z "${VER}" ]]; then
    echo "Usage: $(basename "$0") <version> [out_dir]" >&2
    echo "  version example: 0.3.6  (no leading v)" >&2
    exit 1
fi
VER="${VER#v}"

mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

# shellcheck source=/dev/null
source "${ROOT}/scripts/release/project.env"

SRC_BOOT="${ROOT}/Matter-Bootloader_113W/artifact/Matter-Bootloader_113W.s37"
SRC_APP="${ROOT}/ZigbeeMatterLight_113W/artifact/ZigbeeMatterLight_113W.s37"
SRC_FULL="${ROOT}/artifact/ZigbeeMatterLightSolution_SixG301M113W-full.s37"

ZIP_NAME="${IKEA_PROJECT_ID}-${VER}-unsigned.zip"
STAGE="${OUT_DIR}/.pack_staging_${VER}"
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

cp -a "${SRC_BOOT}" "${STAGE}/Matter-Bootloader_113W.s37"
cp -a "${SRC_APP}" "${STAGE}/ZigbeeMatterLight_113W.s37"

# Optional convenience file for one-shot flash (not required in zip spec).
if [[ -f "${SRC_FULL}" ]]; then
    cp -a "${SRC_FULL}" "${STAGE}/ZigbeeMatterLightSolution_SixG301M113W-full.s37"
fi

rm -f "${OUT_DIR}/${ZIP_NAME}"
(
    cd "${STAGE}"
    zip -r "${OUT_DIR}/${ZIP_NAME}" .
)

cat > "${OUT_DIR}/RELEASE_NOTES.md" <<EOF
# KAJEN_F / SixG301 release ${VER}

- Package: ${ZIP_NAME}
- Project ID: ${IKEA_PROJECT_ID}
- Commit: \${GITHUB_SHA:-local}

## Flash files (inside zip)

| File | Purpose |
|---|---|
| \`Matter-Bootloader_113W.s37\` | Bootloader (flash separately) |
| \`ZigbeeMatterLight_113W.s37\` | Application firmware (flash separately) |

OTA artifacts are not included in this release (see future OTA tutorial).

## Local flash (combined image, if present in repo)

\`\`\`bash
./flash.sh -i artifact/ZigbeeMatterLightSolution_SixG301M113W-full.s37
\`\`\`
EOF

rm -rf "${STAGE}"

echo "Packaged release zip:"
echo "  ${OUT_DIR}/${ZIP_NAME}"
ls -la "${OUT_DIR}/${ZIP_NAME}"
echo "Contents:"
unzip -l "${OUT_DIR}/${ZIP_NAME}"
