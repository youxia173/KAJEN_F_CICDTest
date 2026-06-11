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
CMOCKA_PATH="${SCRIPT_PATH}/../../third_party/cmocka/"

if [ ! -d "${SCRIPT_PATH}/../../third_party" ];then
  mkdir ${SCRIPT_PATH}/../../third_party
fi

if [ ! -d "${CMOCKA_PATH}" ]; then
  echo "cmocka install"
  git clone https://git.cryptomilk.org/projects/cmocka.git ${CMOCKA_PATH}
  sudo apt-get install cmake build-essential libcmocka0 libcmocka-dev -y
else
  echo "cmocka already installed"
fi

pushd $SCRIPT_PATH

mkdir -p "${HOME}/opt"

if [ -d "${CMOCKA_PATH}/build" ]; then
    rm -rf $CMOCKA_PATH/build
fi

cmake -DCMAKE_INSTALL_PREFIX=${HOME}/opt -DCMAKE_BUILD_TYPE=Debug -S $CMOCKA_PATH -B $CMOCKA_PATH/build

make -C $CMOCKA_PATH/build
make install -C $CMOCKA_PATH/build

pip install gcovr

popd
