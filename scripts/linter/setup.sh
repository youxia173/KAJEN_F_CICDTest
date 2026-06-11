#!/bin/bash

SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

if [ ! -d "${SCRIPT_PATH}/../../third_party" ];then
   mkdir ${SCRIPT_PATH}/../../third_party
fi

if [ ! -d "${SCRIPT_PATH}/../../third_party/cpplint" ]; then
   echo "cpplint install"
   git clone https://github.com/cpplint/cpplint.git ${SCRIPT_PATH}/../../third_party/cpplint
else
   echo "cpplint already installed"
fi

