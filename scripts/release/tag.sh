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
    echo -e "Usage: $0 [-r <remote>][-p <project>] [-h]\n"
}

help()
{
  usage
    echo -e "DESCRIPTION:"
    echo -e "This script tags a specific project with a release version."
    echo -e
    echo -e "Options:"
    echo -e "  -r <remote>          Specify the remote repository (default: origin)"
    echo -e "  -p <project>         Specify the project name"
    echo -e "  -h                   Show this help message"
}

# Function to tag the project
tag_project()
{
    local MATTER_CONFIG_FILE="./config/sl_matter_config.h"

    local VERSION=$(grep -m1 -oP '(?<=#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING )"[0-9]+\.[0-9]+\.[0-9]+"' "${MATTER_CONFIG_FILE}" | tr -d '"')
    if [ -z "${VERSION}" ]; then
        echo "Error: Could not find version in ${MATTER_CONFIG_FILE}"
        exit 1
    fi

    local RELEASE_TAG="silabs_mg301-${SINGLE_PROJECT}-${VERSION}"
    echo "Release project: ${SINGLE_PROJECT}-${VERSION}"
    # Check tag already exists
    if git rev-parse "${RELEASE_TAG}" >/dev/null 2>&1; then
        echo "Tag ${RELEASE_TAG} already exists. Skipping tag creation."
    else
        git tag -a "${RELEASE_TAG}" -m "Release ${RELEASE_TAG}"
        if [ $? -eq 0 ]; then
            echo "Tag ${RELEASE_TAG} created successfully."
            echo "git push ${REMOTE} ${RELEASE_TAG}"
            git push ${REMOTE} "${RELEASE_TAG}"
        else
            echo "Failed to create tag ${RELEASE_TAG}."
            exit 1
        fi
    fi
}

# Set default remote
REMOTE="origin"

# Main script execution
unset SINGLE_PROJECT
unset OPTIND

while getopts r:p:h opts; do
    case "${opts}" in
        r) REMOTE="$OPTARG";;
        p) SINGLE_PROJECT="$OPTARG";;
        h) help; exit 0;;
        *) help; exit 1;;
    esac
done

if [ -z "${SINGLE_PROJECT}" ]; then
    echo "Error: No project specified."
    usage
    exit 1
fi

CURRENT_PATH=$(pwd)
SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
# BRANCH_NAME=$(git rev-parse --abbrev-ref HEAD)

cd "${SCRIPT_PATH}/../../kt_projects" || { echo "Error: Failed to change directory to ${SCRIPT_PATH}/../../kt_projects"; exit 1; }

if [ ! -d "${SINGLE_PROJECT}" ]; then
    echo "Error: Project ${SINGLE_PROJECT} does not exist!"
    exit 1
fi

cd "${SINGLE_PROJECT}" || { echo "Error: Failed to change directory to ${SINGLE_PROJECT}"; exit 1; }
tag_project
cd "${CURRENT_PATH}" || { echo "Error: Failed to return to original directory ${CURRENT_PATH}"; exit 1; }
