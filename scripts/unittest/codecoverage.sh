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

SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
IHS_ROOT="${SCRIPT_PATH}/../../kt_components"
CC_DIR="${SCRIPT_PATH}/code_coverage"
OBJ_DIR="${SCRIPT_PATH}/build"

OUTPUT=stdout
FAIL_UNDER_LINE=0
CODE_COV_PERCENT=80

function usage()
{
    # Display Help
    echo "Prints code coverage of the host unit tests. Defaults to stdout text report"
    echo
    echo "Syntax: $0 [-h|-t|-w|-f]"
    echo "OPTIONS:"
    echo "-h        Print this Help."
    echo "-t        Generate text report to file"
    echo "-w        Generate html report to file"
    echo "-j        Generate json report"
    echo
}

if ! type "gcovr" > /dev/null; then
    echo "Error: You need to install gcovr to run this script!"; exit 1;
fi

while getopts ":hwjtf" option;
do
    case "${option}" in
        h)  usage; exit;;
        w)  OUTPUT=html; shift;;
        j)  OUTPUT=json; shift;;
        t)  OUTPUT=text; shift;;
        # Only relevant for pipeline. Default 80% line coverage.
        f)  FAIL_UNDER_LINE=1; shift;
            if [ -n "$1" ]; then
                CODE_COV_PERCENT="$1"
            fi
            ;;
        *) echo "Error: Invalid option"; exit 1;;
    esac
done

# Check that the files are built.
if [ ! -d "${OBJ_DIR}" ]
then
  echo "Error: Code coverage files does not exits, run runner.sh to generate them."
  exit 1
fi

PARAMS=
if [ "${FAIL_UNDER_LINE}" == 1 ]; then
    PARAMS="${PARAMS} --fail-under-line ${CODE_COV_PERCENT}"
fi

# Set CC_DIR to WORKDIR
mkdir -p "${CC_DIR}"

# Check if any tests exitst in OBJ_DIR
RESULT=$(find "${OBJ_DIR}" -name "*test*")

# If no test were found, we just exit otherwise gcovr will fail.
if [ -z "${RESULT}" ]; then
    echo -e "No test were executed. Exiting."; exit;
fi

case "$OUTPUT" in
    html) # Generate html files in out/code_coverage dir.
        gcovr "${OBJ_DIR}"/*/CMakeFiles/*/ -r "${IHS_ROOT}" -p -e '.*test.*' -e '.*mock.*' -e '.*\.h' \
           --exclude-directories '.*third_party.*' --exclude-directories '.*protobuf.*' --html-details "${CC_DIR}"/coverage.html ${PARAMS}
        echo "Generated html report (copy this path into browser to open):"
        echo "file://${CC_DIR}/coverage.html"
        ;;
    json) # Generate html files in out/code_coverage dir.
        gcovr "${OBJ_DIR}"/*/CMakeFiles/*/ -r "${IHS_ROOT}" -p -e '.*test.*' -e '.*mock.*' -e '.*\.h' \
            --exclude-directories '.*third_party.*' --exclude-directories '.*protobuf.*' --json "${CC_DIR}"/coverage.json ${PARAMS}
        echo "Generated json report:"
        echo "file://${CC_DIR}/coverage.json"
        ;;
    text) # Generate txt file in out/code_coverage dir
        gcovr "${OBJ_DIR}"/*/CMakeFiles/*/ -r "${IHS_ROOT}" -p -e '.*test.*' -e '.*mock.*' -e '.*\.h' \
            --exclude-directories '.*third_party.*' --exclude-directories '.*protobuf.*' --fail-under-line 80  -o "${CC_DIR}"/coverage.txt ${PARAMS}
        echo "Generated text report:"
        echo "file://${CC_DIR}/coverage.txt"
        ;;
    stdout)
        gcovr "${OBJ_DIR}"/*/CMakeFiles/*/ -r "${IHS_ROOT}" -p -e '.*test.*' -e '.*mock.*' -e '.*\.h' \
	       --exclude-directories '.*third_party.*' --exclude-directories '.*protobuf.*' ${PARAMS}
        ;;
    *)  echo "Error: Invalid output format"; exit 1;;
esac
