#!/usr/bin/env bash
set -euo pipefail

# Usage: scripts/make.sh [make-target]
TARGET="${1:-all}"

make "${TARGET}"
