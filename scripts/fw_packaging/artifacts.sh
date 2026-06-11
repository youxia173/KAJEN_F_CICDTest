#!/bin/bash
#
# Copyright © Inter IKEA Systems B.V. 2017, 2018, 2019, 2020, 2021.
# All Rights Reserved.
#
# This is UNPUBLISHED PROPRIETARY SOURCE CODE of © Inter IKEA Systems B.V.;
# the contents of this file may not be disclosed to third parties, copied
# or duplicated in any form, in whole or in part, without the prior
# written permission of © Inter IKEA Systems B.V.
#

usage()
{
    echo -e "Usage: $0 [-p <path/to/project>|-t <debug/dev/release>|-h]\n"
}

help()
{
  usage
    echo -e "DESCRIPTION:"
    echo -e "This script runs build priject.."
    echo -e
    echo -e "OPTIONS:"
    echo -e "  -p   Build only specified project"
    echo -e "  -t   Build debug/dev/release"
    echo -e "  -h   Prints this help\n"
}

unset SINGLE_PROJECT
unset BUILD_TYPE
unset OPTIND
while getopts p:,t:h opts
do
    case "${opts}" in
        p)  SINGLE_PROJECT="$OPTARG";;
        t)  BUILD_TYPE="$OPTARG";;
        h)  help;;
        \?) help;;
    esac
done

BUILD_UPGRADE=1

# ------------------------------
# Shared Build Function
# ------------------------------
_COMMON_BUILD_ARTIFACTS() {
    local build_type="$1"          # Build type (debug/debug-nocd/dev/dev-nocd/release)
    local out_path="$2"            # Output directory path
    local sign_path="$3"           # Signature directory path
    local copy_hex="${4:-true}"    # Whether to copy hex files (default: true)

    # Map build types to SDK subdirectories
    local build_subdir="$build_type"

    # local sdk_dir="../../sdk/Work/$(basename "$PWD")_mg301/${build_subdir}"
    local fw_dir="./fw_output/${build_subdir}"
    
    echo "Building $build_type (Firmware path: $fw_dir)"
    
    # Execute build command
    if ! make "$build_type"; then
        echo "Build failed for $build_type"
        return 1
    fi

    # Copy artifacts
    if [ "$copy_hex" = "true" ]; then
        # Use array to handle glob expansion
        files=(${fw_dir}/*)
        cp ./token/* $out_path
        cp ./bootloader/bootloader-SIMG301M113WIH-3072K-secureboot*.s37 $out_path
        cp ./factory_test/*.s37 $out_path
    else
        cp ${fw_dir}/*Silabs-${build_type}.s37 $out_path
        files=(${fw_dir}/*.ota)
    fi

    if [ -e "${files[0]}" ]; then
        cp "${files[@]}" "$out_path"
    else
        echo "Warning: No files found in $fw_dir matching pattern"
    fi

    make clean
}

# ------------------------------
# Build Project Function
# ------------------------------
function build_project() {
    PROJECT_NAME=$(basename "$PWD")
    VERSION=$( cat ./config/sl_matter_config.h | grep -m 1 "#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING" | cut -d ' ' -f 3 | tr -d '"' )
    echo "Building: $PROJECT_NAME-$VERSION"
    OUT_PRJ_PATH="${OUT_PATH}/${PROJECT_NAME}-${VERSION}"
    mkdir -p "$OUT_PRJ_PATH" || return 1

    if [ -n "${BUILD_TYPE}" ]; then
        # Single build type mode
        case "${BUILD_TYPE}" in
            debug)
                _COMMON_BUILD_ARTIFACTS "debug" "$OUT_PRJ_PATH" ""
                ;;
            dev)
                _COMMON_BUILD_ARTIFACTS "dev" "$OUT_PRJ_PATH" ""
                ;;
            release)
                _COMMON_BUILD_ARTIFACTS "release" "$OUT_PRJ_PATH" ""
                ;;
            *)
                echo "Unsupported build type: ${BUILD_TYPE}"
                return 1
                ;;
        esac
    else
        # Full build mode with new type
        for bt in debug dev release; do
            _COMMON_BUILD_ARTIFACTS "$bt" "$OUT_PRJ_PATH" ""
        done
    fi
}

# ------------------------------
# Build Test Upgrade Function
# ------------------------------
build_test_upgrade() {
    PROJECT_NAME=$(basename "$PWD")
    VERSION=$( cat ./config/sl_matter_config.h | grep -m 1 "#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING" | cut -d ' ' -f 3 | tr -d '"' )
    echo "Building Test Upgrade: $PROJECT_NAME-$VERSION"
    
    OUT_TEST_UPGRADE_PATH="${OUT_PATH}/${PROJECT_NAME}-${VERSION}/test_upgrade"
    mkdir -p "$OUT_TEST_UPGRADE_PATH" || return 1

    # Version increment
    source "${UPDATE_SCRIPT}" -i -p "${PROJECT_NAME}" || return 1
    source ${ACTIVE_SCRIPT}

    if [ -n "${BUILD_TYPE}" ]; then
        case "${BUILD_TYPE}" in
            debug)
                _COMMON_BUILD_ARTIFACTS "debug" "$OUT_TEST_UPGRADE_PATH" "" "false"
                ;;
            dev)
                _COMMON_BUILD_ARTIFACTS "dev" "$OUT_TEST_UPGRADE_PATH" "" "false"
                ;;
            release)
                _COMMON_BUILD_ARTIFACTS "release" "$OUT_TEST_UPGRADE_PATH" "" "false"
                ;;
            *)
                echo "Unsupported build type: ${BUILD_TYPE}"
                return 1
                ;;
        esac
    else
        # Full test build cycle
        for bt in debug dev release; do
            _COMMON_BUILD_ARTIFACTS "$bt" "$OUT_TEST_UPGRADE_PATH" "" "false"
        done
    fi

    # Version restoration
    source "${UPDATE_SCRIPT}"  -d -p "${PROJECT_NAME}" || return 1
}

# ------------------------------
# Update Min Version Function
# ------------------------------
update_min_version()
{
    local MATTER_CONFIG_FILE="./config/sl_matter_config.h"

    # Update the hexadecimal version
    local CURRENT_VERSION=$(grep -m1 -oP '(?<=#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION )0x[0-9A-Fa-f]+' "$MATTER_CONFIG_FILE")
    if [ -z "$CURRENT_VERSION" ]; then
        echo "Error: Could not find current version in $MATTER_CONFIG_FILE"
        exit 1
    fi

    local NEW_MIN_VERSION=$(printf "0x%08X" $((CURRENT_VERSION)))

    sed -i "s/^#define CHIP_DEVICE_CONFIG_MIN_APPLICABLE_SOFTWARE_VERSION .*/#define CHIP_DEVICE_CONFIG_MIN_APPLICABLE_SOFTWARE_VERSION $NEW_MIN_VERSION/" "$MATTER_CONFIG_FILE"
   
    echo "Hexadecimal version updated to $NEW_MIN_VERSION"
}

# ------------------------------
# Main Script Execution
# ------------------------------
CURRENT_PATH=$(pwd)
echo "CURRENT_PATH: ${CURRENT_PATH}"
SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
echo "SCRIPT_PATH: ${SCRIPT_PATH}"
ACTVIE_PRJ=${SCRIPT_PATH}/active_projects
echo "ACTVIE_PRJ: ${ACTVIE_PRJ}"
PRJ_PATH=${SCRIPT_PATH}/../../kt_projects
echo "PRJ_PATH: ${PRJ_PATH}"
OUT_PATH=${SCRIPT_PATH}/../../artifacts
echo "OUT_PATH: ${OUT_PATH}"

ACTIVE_SCRIPT=${SCRIPT_PATH}/../../scripts/setup/activate.sh
SDK_PATCH=${SCRIPT_PATH}/../../patches/apply_patches.sh
UPDATE_SCRIPT=${SCRIPT_PATH}/../../scripts/release/update_version.sh

rm -rf   "${OUT_PATH}"
mkdir -p "${OUT_PATH}"

# source ${ACTIVE_SCRIPT}

cd ${PRJ_PATH}

if [ -n "${SINGLE_PROJECT}" ]; then
    echo "Build only project: ${SINGLE_PROJECT}"
    cd ${PRJ_PATH}/${SINGLE_PROJECT}
    build_project
else
    echo "Build all projects"
    dir=$(ls -l . |awk '/^d/ {print $NF}')
    for i in $dir
    do
        if [ `grep -c $i $ACTVIE_PRJ` -ne '0' ];then
            cd ${PRJ_PATH}/$i
            build_project
        fi
    done
fi

echo "BUILD_UPGRADE: ${BUILD_UPGRADE}"
if [ "${BUILD_UPGRADE}" == 1 ]; then
    cd ${CURRENT_PATH}
    cd ${PRJ_PATH}

    if [ -n "${SINGLE_PROJECT}" ]; then
        echo "Build only upgrade project: ${SINGLE_PROJECT}"
        cd ${PRJ_PATH}/${SINGLE_PROJECT}
        build_test_upgrade
    else
        echo "Build all upgrade projects"
        dir=$(ls -l . |awk '/^d/ {print $NF}')
        for i in $dir
        do
            if [ `grep -c $i $ACTVIE_PRJ` -ne '0' ];then
                cd ${PRJ_PATH}/$i
                build_test_upgrade
            fi
        done
    fi

    cd ${CURRENT_PATH}
fi

echo "Create artifacts.tar.gz"
cd ${OUT_PATH}
tar -zcvf ${OUT_PATH}/artifacts.tar.gz *
cd ${CURRENT_PATH}
