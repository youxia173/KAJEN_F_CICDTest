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

set -e

usage() 
{
    echo -e "Usage: $0 [-i|-d] [-p <project>] [-h]\n"
}

help() 
{
  usage
    echo -e "DESCRIPTION:"
    echo -e "This script runs to update the project version."
    echo -e
    echo -e "Options:"
    echo -e "  -i                   Increment version"
    echo -e "  -d                   Decrement version"
    echo -e "  -p <project>         Specify the project name"
    echo -e "  -h                   Show this help message"
}

# Function to update the version
update_version()
{
    local MATTER_CONFIG_FILE="./config/sl_matter_config.h"
    local ZIGBEE_CONFIG_FILES="./config/common/zcl_config.zap"

    # Update the hexadecimal version
    # =$(grep -m1 -oP '(?<=#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION )0x[0-9A-Fa-f]+' "$MATTER_CONFIG_FILE")
    local CURRENT_VERSION=$( cat $MATTER_CONFIG_FILE | grep -m 1 "#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION" | cut -d ' ' -f 3 | tr -d '"' )
    if [ -z "$CURRENT_VERSION" ]; then
        echo "Error: Could not find current version in $MATTER_CONFIG_FILE"
        exit 1
    fi

    local NEW_VERSION=$(printf "%d" $((CURRENT_VERSION + UPDATE_VALUE)))
    local TEST_VERSION=$(printf "%d" $((CURRENT_VERSION + 1)))
    local NEW_TEST_VERSION=$(printf "%d" $((TEST_VERSION + UPDATE_VALUE)))
    local MAX_VERSION=$(printf "%d" $((CURRENT_VERSION - 1)))

    if [ $UPDATE_VALUE -gt 0 ]; then
        sed -i "s/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION $TEST_VERSION/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION $NEW_TEST_VERSION/" "$MATTER_CONFIG_FILE"
        sed -i "s/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION $CURRENT_VERSION/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION $NEW_VERSION/" "$MATTER_CONFIG_FILE"
    else
        sed -i "s/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION $CURRENT_VERSION/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION $NEW_VERSION/" "$MATTER_CONFIG_FILE"
        sed -i "s/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION $TEST_VERSION/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION $NEW_TEST_VERSION/" "$MATTER_CONFIG_FILE"
    fi
    echo "Software version updated from $CURRENT_VERSION to $NEW_VERSION"
    echo "Software test version updated from $TEST_VERSION to $NEW_TEST_VERSION"

    # Update the string version
    local CURRENT_VERSION_STRING=$(grep -m1 -oP '(?<=#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING )"[0-9]+\.[0-9]+\.[0-9]+"' "$MATTER_CONFIG_FILE")
    # local CURRENT_VERSION_STRING=$( cat $MATTER_CONFIG_FILE | grep -m 1 "#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING" | cut -d ' ' -f 3 | tr -d '"' )
    if [ -z "$CURRENT_VERSION_STRING" ]; then
        echo "Error: Could not find current version string in $MATTER_CONFIG_FILE"
        exit 1
    fi

    local MAJOR=$(echo "$CURRENT_VERSION_STRING" | awk -F'[".]' '{print $2}')
    local MINOR=$(echo "$CURRENT_VERSION_STRING" | awk -F'[".]' '{print $3}')
    local PATCH=$(echo "$CURRENT_VERSION_STRING" | awk -F'[".]' '{print $4}')
    local NEW_VERSION_STRING="\"$MAJOR.$MINOR.$((PATCH + UPDATE_VALUE))\""
    local TEST_VERSION_STRING="\"$MAJOR.$MINOR.$((PATCH + 1))\""
    local NEW_TEST_VERSION_STRING="\"$MAJOR.$MINOR.$((PATCH + 1 + UPDATE_VALUE))\""

    if [ $UPDATE_VALUE -gt 0 ]; then
        sed -i "s/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING $TEST_VERSION_STRING/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING $NEW_TEST_VERSION_STRING/" "$MATTER_CONFIG_FILE"
        sed -i "s/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING $CURRENT_VERSION_STRING/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING $NEW_VERSION_STRING/" "$MATTER_CONFIG_FILE"
    else
        sed -i "s/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING $CURRENT_VERSION_STRING/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING $NEW_VERSION_STRING/" "$MATTER_CONFIG_FILE"
        sed -i "s/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING $TEST_VERSION_STRING/#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING $NEW_TEST_VERSION_STRING/" "$MATTER_CONFIG_FILE"
    fi
    echo "String version updated from $CURRENT_VERSION_STRING to $NEW_VERSION_STRING"
    echo "String test version updated from $TEST_VERSION_STRING to $NEW_TEST_VERSION_STRING"

    # Update zigbee configuration files
    for ZIGBEE_CONFIG_FILE in "${ZIGBEE_CONFIG_FILES[@]}"; do
        if [ -f "$ZIGBEE_CONFIG_FILE" ]; then
            sed -i "s/$CURRENT_VERSION_STRING/$NEW_VERSION_STRING/" "$MATTER_CONFIG_FILE"
        else
            echo "Warning: $ZIGBEE_CONFIG_FILE does not exist, skipping..."
        fi
    done
}

# Main script execution
unset SINGLE_PROJECT
unset OPTIND
UPDATE_VALUE=1

while getopts p:idh opts; do
    case "${opts}" in
        p) SINGLE_PROJECT="$OPTARG";;
        i) UPDATE_VALUE=1;;
        d) UPDATE_VALUE=-1;;
        h) help; exit 0;;
        *) help; exit 1;;
    esac
done

if [ -z "${SINGLE_PROJECT}" ]; then
    echo "Error: No project specified."
    usage
    exit 1
fi

ORIGINAL_PATH=$(pwd)
SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd "${SCRIPT_PATH}/../../kt_projects" || { echo "Error: Failed to change directory to ${SCRIPT_PATH}/../../kt_projects"; exit 1; }

if [ ! -d "${SINGLE_PROJECT}" ]; then
    echo "Error: Project ${SINGLE_PROJECT} does not exist!"
    exit 1
fi

cd "${SINGLE_PROJECT}" || { echo "Error: Failed to change directory to ${SINGLE_PROJECT}"; exit 1; }
update_version
cd "${ORIGINAL_PATH}" || { echo "Error: Failed to return to original directory ${ORIGINAL_PATH}"; exit 1; }