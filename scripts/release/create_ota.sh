#!/usr/bin/env bash
# Create Matter OTA package from application .s37 via Simplicity Commander.
#
# MG301 (SixG301): commander gbl4 create … --data app.s37 --compress lzma
# MG24:            commander gbl  create … --app  app.s37 --compress lzma
#
# Usage:
#   bash scripts/release/create_ota.sh <app.s37> <out_dir> <sw_string> <sw_version>
#
# Writes:
#   <out_dir>/firmware.gbl   (intermediate; not required in IKEA zip)
#   <out_dir>/firmware.ota
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

APP_S37="${1:-}"
OUT_DIR="${2:-}"
SW_STRING="${3:-}"
SW_VERSION="${4:-}"

if [[ -z "${APP_S37}" || -z "${OUT_DIR}" || -z "${SW_STRING}" || -z "${SW_VERSION}" ]]; then
    echo "Usage: $(basename "$0") <app.s37> <out_dir> <sw_string> <sw_version>" >&2
    echo "  example: $(basename "$0") app.s37 /tmp/out 1.1.0 0x01010001" >&2
    exit 1
fi

if [[ ! -f "${APP_S37}" ]]; then
    echo "ERROR: application image not found: ${APP_S37}" >&2
    exit 1
fi

# shellcheck source=/dev/null
source "${ROOT}/scripts/release/project.env"

find_commander() {
    if [[ -n "${COMMANDER:-}" && -x "${COMMANDER}" ]]; then
        echo "${COMMANDER}"
        return
    fi
    if command -v commander >/dev/null 2>&1; then
        command -v commander
        return
    fi
    local candidates=(
        "${HOME}/.silabs/slt/bin/commander"
        "${HOME}/.silabs/slt/installs/archive/commander/commander"
        "/opt/siliconlabs/commander/commander"
    )
    local c
    for c in "${candidates[@]}"; do
        if [[ -x "${c}" ]]; then
            echo "${c}"
            return
        fi
    done
    echo "ERROR: simplicity commander not found. Set COMMANDER=/path/to/commander" >&2
    exit 1
}

CMD="$(find_commander)"
mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

GBL_OUT="${OUT_DIR}/firmware.gbl"
OTA_OUT="${OUT_DIR}/firmware.ota"
GBL_MODE="${OTA_GBL_CMD:-gbl4}"
VENDOR_ID="${OTA_VENDOR_ID:-0xFFF1}"
PRODUCT_ID="${OTA_PRODUCT_ID:-0x8005}"
COMPRESS="${OTA_COMPRESS:-lzma}"
DEVICE_ARGS=()
if [[ -n "${OTA_DEVICE:-}" ]]; then
    DEVICE_ARGS=(--device "${OTA_DEVICE}")
fi

rm -f "${GBL_OUT}" "${OTA_OUT}"

echo "Using commander: ${CMD}"
echo "GBL mode: ${GBL_MODE}  compress: ${COMPRESS}"
echo "OTA VID/PID: ${VENDOR_ID} / ${PRODUCT_ID}"
echo "OTA version: ${SW_VERSION} (${SW_STRING})"

case "${GBL_MODE}" in
    gbl4)
        "${CMD}" gbl4 create "${GBL_OUT}" \
            "${DEVICE_ARGS[@]}" \
            --data "${APP_S37}" \
            --compress "${COMPRESS}"
        ;;
    gbl|gbl3)
        "${CMD}" gbl create "${GBL_OUT}" \
            "${DEVICE_ARGS[@]}" \
            --app "${APP_S37}" \
            --compress "${COMPRESS}"
        ;;
    *)
        echo "ERROR: unsupported OTA_GBL_CMD='${GBL_MODE}' (use gbl4 or gbl)" >&2
        exit 1
        ;;
esac

OTA_EXTRA=()
if [[ -n "${IKEA_OTA_MIN_VERSION:-}" ]]; then
    OTA_EXTRA+=(--min-sw "${IKEA_OTA_MIN_VERSION}")
fi

"${CMD}" ota create --type matter \
    --input "${GBL_OUT}" \
    --vendorid "${VENDOR_ID}" \
    --productid "${PRODUCT_ID}" \
    --swstring "${SW_STRING}" \
    --swversion "${SW_VERSION}" \
    --digest sha256 \
    "${OTA_EXTRA[@]}" \
    -o "${OTA_OUT}"

ls -la "${GBL_OUT}" "${OTA_OUT}"
echo "Created: ${OTA_OUT}"
