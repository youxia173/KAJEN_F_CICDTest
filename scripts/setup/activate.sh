#!/bin/bash

SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

if [ ! -d "${SCRIPT_PATH}/../../sdk" ]; then
    source ${SCRIPT_PATH}/../../scripts/setup/checkout.sh
fi

# if [ $(whereis simplicity-commander) >/dev/null 2>&1 ]; then
#     echo "Install SimplicityCommander-Linux"
#     wget https://www.silabs.com/documents/login/software/SimplicityCommander-Linux.zip
#     unzip SimplicityCommander-Linux.zip
#     Commander_linux_ver=$(find . -type f -name "Commander_linux_x86_64_*" | head -n 1)
#     sudo tar -xjvf $Commander_linux_ver --directory /opt/
#     sudo ln -s /opt/commander/commander /usr/bin/simplicity-commander
#     rm SimplicityCommander-Linux.zip
#     rm -rf SimplicityCommander-Linux
# else
#     echo "Already install SimplicityCommander-Linux"
#     simplicity-commander -v
# fi  

echo "Setup Matter environment"
# cd ${SCRIPT_PATH}/../../sdk
# source ./scripts/bootstrap.sh
# source ./scripts/activate.sh
# cd -
pip install dload
pip install dotenv

# Determine active project from fw_packaging/active_projects if present
ACTIVE_PROJECTS_FILE="${SCRIPT_PATH}/../fw_packaging/active_projects"
if [ -f "$ACTIVE_PROJECTS_FILE" ]; then
    PROJECT_NAME=$(head -n 1 "$ACTIVE_PROJECTS_FILE" | tr -d ' \t\r\n')
    if [ -z "$PROJECT_NAME" ]; then
        echo "active_projects is empty, falling back to 'efr32_default_combo'"
        PROJECT_NAME="efr32_default_combo"
    fi
else
    echo "active_projects not found, falling back to 'efr32_default_combo'"
    PROJECT_NAME="efr32_default_combo"
fi

cd ${SCRIPT_PATH}/../../kt_projects/${PROJECT_NAME} || { echo "Failed to cd to project: ${PROJECT_NAME}"; exit 1; }
source ./generate.sh


