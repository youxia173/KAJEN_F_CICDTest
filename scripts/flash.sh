#!/usr/bin/env bash
# SixG301 (SIMG301M113WIH) flash script — bypasses Simplicity Studio SDM flash.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMAKE_DIR="${ROOT}/ZigbeeMatterLightSolution_SixG301M113W_cmake"
IMAGE="${IMAGE:-${ROOT}/artifact/ZigbeeMatterLightSolution_SixG301M113W-full.s37}"
DEVICE="${DEVICE:-SIMG301M113WIH}"
# BRD4002A + SixG301; override: JLINK_SERIAL=xxxxx ./scripts/flash.sh
JLINK_SERIAL="${JLINK_SERIAL:-440353167}"

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
        "${HOME}/.silabs/slt/installs/archive/commander/commander"
        "/opt/siliconlabs/commander/commander"
    )
    for c in "${candidates[@]}"; do
        if [[ -x "${c}" ]]; then
            echo "${c}"
            return
        fi
    done
    echo "ERROR: simplicity_commander not found. Set COMMANDER=/path/to/commander" >&2
    exit 1
}

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Flash ZigbeeMatterLight firmware to SixG301 (SIMG301M113WIH).

Options:
  -b, --build       Build firmware first, then flash
  -i, --image FILE  Firmware image (default: artifact/...-full.s37)
  -s, --serial NUM  J-Link serial number (default: ${JLINK_SERIAL})
  -d, --device PART Target device (default: ${DEVICE})
  -l, --list        List connected J-Link adapters and exit
  -h, --help        Show this help

Examples:
  ./scripts/flash.sh                  # flash only
  ./scripts/flash.sh --build          # build + flash
  JLINK_SERIAL=440353167 ./scripts/flash.sh
EOF
}

list_adapters() {
    local cmd
    cmd="$(find_commander)"
    echo "Connected J-Link adapters:"
    "${cmd}" adapter list 2>/dev/null || "${cmd}" adapter probe 2>&1 || true
}

build_firmware() {
    echo "==> Building firmware..."
    if [[ ! -d "${CMAKE_DIR}" ]]; then
        echo "ERROR: CMake project not found: ${CMAKE_DIR}" >&2
        exit 1
    fi
    (cd "${CMAKE_DIR}" && cmake --build --preset default_config)
    echo "==> Build done."
}

do_flash() {
    local cmd
    cmd="$(find_commander)"

    if [[ ! -f "${IMAGE}" ]]; then
        echo "ERROR: Firmware not found: ${IMAGE}" >&2
        echo "Run with --build first, or build manually:" >&2
        echo "  cd ${CMAKE_DIR} && cmake --build --preset default_config" >&2
        exit 1
    fi

    if ! "${cmd}" adapter probe --serialno "${JLINK_SERIAL}" >/dev/null 2>&1; then
        echo "ERROR: J-Link ${JLINK_SERIAL} not found." >&2
        list_adapters
        echo "Use --list to see adapters, or --serial <number> to pick one." >&2
        exit 1
    fi

    echo "==> Flashing"
    echo "    Image : ${IMAGE}"
    echo "    Device: ${DEVICE}"
    echo "    J-Link: ${JLINK_SERIAL}"
    echo

    "${cmd}" flash "${IMAGE}" \
        --device "${DEVICE}" \
        --serialno "${JLINK_SERIAL}" \
        --halt

    echo
    echo "==> Flash OK"
}

DO_BUILD=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        -b|--build) DO_BUILD=1; shift ;;
        -i|--image) IMAGE="$2"; shift 2 ;;
        -s|--serial) JLINK_SERIAL="$2"; shift 2 ;;
        -d|--device) DEVICE="$2"; shift 2 ;;
        -l|--list) list_adapters; exit 0 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

if [[ "${DO_BUILD}" -eq 1 ]]; then
    build_firmware
fi
do_flash
