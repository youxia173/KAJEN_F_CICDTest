#!/usr/bin/env bash
# Install host unit-test dependencies (cmocka + gcovr). IKEA MG301 style.
set -euo pipefail

SCRIPT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

need_sudo=0
if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
  need_sudo=1
fi

install_apt() {
  if [[ "${need_sudo}" -eq 1 ]]; then
    sudo apt-get "$@"
  else
    apt-get "$@"
  fi
}

if command -v apt-get >/dev/null 2>&1; then
  if ! pkg-config --exists cmocka || ! command -v gcovr >/dev/null 2>&1; then
    install_apt update
    install_apt install -y --no-install-recommends \
      build-essential cmake pkg-config libcmocka-dev gcovr python3
  fi
fi

if ! pkg-config --exists cmocka; then
  echo "ERROR: libcmocka-dev not available (pkg-config cmocka missing)" >&2
  exit 1
fi

if ! command -v gcovr >/dev/null 2>&1; then
  echo "ERROR: gcovr not available after install" >&2
  exit 1
fi

echo "Unit test deps OK: cmocka + gcovr"
