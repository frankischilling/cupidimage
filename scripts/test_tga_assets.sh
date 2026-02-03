#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/bin/cupidimage"
ASSETS_DIR="$ROOT_DIR/assets/tga"
WIDTH="${1:-120}"
HEIGHT="${2:-60}"

if [[ ! -x "$BIN" ]]; then
  echo "cupidimage binary not found at $BIN" >&2
  echo "Build it first: make" >&2
  exit 1
fi

if [[ ! -d "$ASSETS_DIR" ]]; then
  echo "assets/tga directory not found" >&2
  exit 1
fi

shopt -s nullglob
files=("$ASSETS_DIR"/*.tga)
if (( ${#files[@]} == 0 )); then
  echo "No .tga files found in $ASSETS_DIR" >&2
  exit 1
fi

for file in "${files[@]}"; do
  echo
  echo "=== $file ==="
  set +e
  "$BIN" "$file" "$WIDTH" "$HEIGHT"
  status=$?
  set -e
  if [[ $status -ne 0 ]]; then
    echo "Note: load failed (exit $status), continuing..."
  fi
  echo
  read -r -p "Press Enter for next image (or Ctrl+C to stop)..." _
  printf "\033c"
done
