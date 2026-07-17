#!/usr/bin/env bash
# Convenience wrapper — run from project root: ./flash.sh [--build]
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scripts/flash.sh" "$@"
