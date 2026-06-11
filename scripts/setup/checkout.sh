#!/bin/bash

SMALLEST_SDK=0

usage()
{
    echo -e "Usage: $0 [-s|-h]\n"
}

help()
{
  usage
    echo -e "DESCRIPTION:"
    echo -e "This script runs pull code.."
    echo -e
    echo -e "OPTIONS:"
    echo -e "  -s   Fetch submodules without history"
    echo -e "  -h   Prints this help\n"
}

unset OPTIND
while getopts s,h opts
do
    case "${opts}" in
        s)  SMALLEST_SDK=1;;
        h)  help; exit;;
        \?) help; exit;;
    esac
done

SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

if [ ! -d "${SCRIPT_PATH}/../../sdk" ]; then
    echo "Clone Silabs Simplicity SDK 2024.12"
    if [ "${SMALLEST_SDK}" == 0 ]; then
        git clone --branch 'sisdk-2024.12' https://github.com/SiliconLabs/simplicity_sdk.git ${SCRIPT_PATH}/../../sdk
    else
        git clone --depth 1 --branch 'sisdk-2024.12' https://github.com/SiliconLabs/simplicity_sdk.git ${SCRIPT_PATH}/../../sdk
    fi
    cd ${SCRIPT_PATH}/../../sdk
    git status
    git submodule update --init --recursive
    git fetch --tags
    if git ls-remote --tags origin "v2024.12.1-0" | grep -q "v2024.12.1-0"; then
        git checkout -b v2024.12.1-0 tags/v2024.12.1-0
    else
        echo "silabs sdk tag v2024.12.1-0 does NOT exist"
    fi
    cd -
else
    echo "Silabs Simplicity SDK 2024.12 already installed"
fi

if [ ! -d "${SCRIPT_PATH}/../../sdk/extension/matter_extension" ]; then
    echo "Clone Silabs Matter 1.4"
    if [ "${SMALLEST_SDK}" == 0 ]; then
        git clone --branch 'main' https://github.com/SiliconLabs/matter_extension.git ${SCRIPT_PATH}/../../sdk/extension/matter_extension
    else
        git clone --depth 1 --branch 'main' https://github.com/SiliconLabs/matter_extension.git ${SCRIPT_PATH}/../../sdk/extension/matter_extension
    fi
    cd ${SCRIPT_PATH}/../../sdk/extension/matter_extension
    git submodule update --init --recursive
    git fetch --tags
    if git ls-remote --tags origin "v2.5.0" | grep -q "v2.5.0"; then
        git checkout -b v2.5.0 tags/v2.5.0
    else
        echo "matter sdk tag v2.5.0 does NOT exist"
    fi
    cd -
else
    echo "Silabs Matter 1.4 already installed"
fi

# echo "Checkout submodules"
# cd ${SCRIPT_PATH}/../../sdk
# ./scripts/checkout_submodules.py --shallow --recursive --platform silabs linux
# cd ${SCRIPT_PATH}/../..

cd ${SCRIPT_PATH}/../../patches
# echo "Apply patcher to sdk"
# source ./apply_patches.sh
echo "Apply SDK application change file to sdk"
source ./apply_files.sh
cd -

