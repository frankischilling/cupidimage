#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   PREFIX=/usr/local scripts/install.sh
#   PREFIX=/usr/local scripts/install.sh --uninstall
PREFIX="${PREFIX:-/usr/local}"

if [[ "${1:-}" == "--uninstall" ]]; then
    rm -f "${PREFIX}/lib/libcupidimage.a" "${PREFIX}/include/cupidimage.h"
    echo "Uninstalled from ${PREFIX}"
    exit 0
fi

make lib

install -d "${PREFIX}/lib" "${PREFIX}/include"
install -m 644 bin/libcupidimage.a "${PREFIX}/lib/"
install -m 644 src/cupidimage.h "${PREFIX}/include/"

echo "Installed to ${PREFIX}"
