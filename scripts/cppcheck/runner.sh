#!/bin/bash

SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

REPORT_PATH=${SCRIPT_PATH}/../../code_quality_report
CPPCHECK_REPORT_PATH=${REPORT_PATH}/cppcheck
CPPCHECK_BIN=${SCRIPT_PATH}/../../third_party/cppcheck/cppcheck
CPPCHECK_HTMLREPORT=${SCRIPT_PATH}/../../third_party/cppcheck/htmlreport/cppcheck-htmlreport

if [ ! -d ${REPORT_PATH} ];then
    mkdir ${REPORT_PATH}
fi

if [ ! -d ${CPPCHECK_REPORT_PATH} ];then
    mkdir ${CPPCHECK_REPORT_PATH}
else
    rm ${CPPCHECK_REPORT_PATH}/*
fi

${CPPCHECK_BIN} -j$(nproc) \
    --enable=warning,style,performance,portability \
    --xml \
    '-D__ALIGNED(x)=' \
    ${SCRIPT_PATH}/../../kt_components/ \
    --suppress=*:*/test/* \
    --suppress=*:*_cli.c \
    &> ${CPPCHECK_REPORT_PATH}/cppcheck_report.xml

${CPPCHECK_HTMLREPORT} --file=${CPPCHECK_REPORT_PATH}/cppcheck_report.xml \
    --title=KEETAT \
    --report-dir=${CPPCHECK_REPORT_PATH} \
    --source-dir=${SCRIPT_PATH}/../../kt_components/
