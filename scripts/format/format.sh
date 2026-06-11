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

INCLUDE_SDK=0
CHECK_DIFF=0

usage()
{
    echo -e "Usage: $0 [-s|-f <path/to/file>|-h]\n"
}

help()
{
  usage
    echo -e "DESCRIPTION:"
    echo -e "This script runs cpplinter on c and h files.."
    echo -e
    echo -e "OPTIONS:"
    echo -e "  -s   Include SDK files to be linted."
    echo -e "  -f   Lint only specified file. Cannot be used with -s flag."
    echo -e "  -d   Check if any diff may exist and fail. Used for pipeline."
    echo -e "  -h   Prints this help\n"
}

while getopts s,f:,d,h opts
do
    # shellcheck disable=SC2213
    case "${opts}" in
        s)  INCLUDE_SDK=1;;
        f)  SINGLE_FILE="$OPTARG";;
        d)  CHECK_DIFF=1;;
        h)  help; exit;;
        \?) help; exit;;
    esac
done

SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

pushd "$PWD" > /dev/null 2>&1

    cd ${SCRIPT_PATH}/../..

    if [ -n "${SINGLE_FILE}" ]; then
        clang-format -style=file -i --verbose "${SINGLE_FILE}"
    else
        patterns=('*.c' '*.h')
        excludes=('./sdk/*' './third_party/*' './unit_test/*' './scripts/*' './kt_projects/*' './patches/*' './sdk_commit_tmp/*')


        if [ "${INCLUDE_SDK}" == 0 ]; then
            excludes+=('./sdk/*')
        fi

        exclude_args=()
        for exclude in "${excludes[@]}"; do
            exclude_args+=('!' -path "$exclude" -a)
        done

        pattern_args=()
        for pattern in "${patterns[@]}"; do
            pattern_args+=(-o -name "$pattern")
        done

        find . "${exclude_args[@]}" '(' "${pattern_args[@]:1}" ')' -type f -exec clang-format -style=file -i --verbose {} +
    fi

    if [ "${CHECK_DIFF}" == 1 ]; then
        git diff -- "*.c" "*.h" > clang_format.patch

        if [ "${INCLUDE_SDK}" == 1 ]; then
            pushd; git diff -- "*.c" "*.h" >> clang_format.patch; popd;
        fi

        if [ ! -s clang_format.patch ]
        then
            rm clang_format.patch; echo "Format OK"; exit;
        else
            echo "Error: Change contains unformatted code."; exit 1;
        fi
    fi

    cd -

popd > /dev/null 2>&1
