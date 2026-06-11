#!/bin/bash

SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

REPORT_PATH=${SCRIPT_PATH}/../../code_quality_report

source ${SCRIPT_PATH}/../linter/linter.sh -p

source ${SCRIPT_PATH}/../cppcheck/runner.sh

tar -zcvf code_quality_report.tar.gz ${REPORT_PATH}
