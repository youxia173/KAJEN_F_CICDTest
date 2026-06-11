#!/usr/bin/env bash

# Copyright © Inter IKEA Systems B.V. 2017, 2018, 2019, 2020, 2021.
# All Rights Reserved.
#
# This is UNPUBLISHED PROPRIETARY SOURCE CODE of © Inter IKEA Systems B.V.;
# the contents of this file may not be disclosed to third parties, copied
# or duplicated in any form, in whole or in part, without the prior
# written permission of © Inter IKEA Systems B.V.

set -e

SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
MY_PWD=${PWD}
RUN_SDK_TESTS=0

usage()
{
    echo -e "Usage: $0 [-s|-f <filter>|-h]\n"
}

help()
{
  usage
    echo -e "DESCRIPTION:"
    echo -e "Execute unittests.."
    echo -e
    echo -e "OPTIONS:"
    echo -e "  -s   Include SDK unittests."
    echo -e "  -f   Filter a specific unittest. Specify -s flag if the test is under the SDK."
    echo -e "  -h   Prints this help\n"
}

while getopts s,f:,h opts
do
    # shellcheck disable=SC2213
    case "${opts}" in
        s)  RUN_SDK_TESTS=1;;
        f)  FILTER="$OPTARG";;
        h)  help; exit;;
        \?) help; exit;;
    esac
done

pushd $SCRIPT_PATH

cmake -DCMAKE_INSTALL_PREFIX=${HOME}/opt \
            -DCMAKE_BUILD_TYPE=Debug \
            -DROOT_DIR="${SCRIPT_PATH}/../.." \
            -DRUN_SDK_TESTS="${RUN_SDK_TESTS}" \
            -DDEVICE_FAMILY="ZINGO" \
            -DFILTER:STRING=$FILTER -B build

make -C build
popd
