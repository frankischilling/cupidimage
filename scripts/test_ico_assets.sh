#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/bin/cupidimage"
ASSETS_DIR="$ROOT_DIR/assets/ico"
WIDTH="${1:-120}"
HEIGHT="${2:-60}"

if [[ ! -x "$BIN" ]]; then
  echo "cupidimage binary not found or not executable at $BIN" >&2
  echo "Build it first: make" >&2
  exit 1
fi

if [[ ! -d "$ASSETS_DIR" ]]; then
  echo "assets/ico directory not found at $ASSETS_DIR" >&2
  exit 1
fi

shopt -s nullglob
files=("$ASSETS_DIR"/*.ico "$ASSETS_DIR"/*.cur)
if (( ${#files[@]} == 0 )); then
  echo "No .ico/.cur files found in $ASSETS_DIR" >&2
  exit 1
fi

for file in "${files[@]}"; do
  if [[ -f "${file}.SKIPPED.txt" ]]; then
    echo "=== $file ==="
    echo "SKIPPED: $(cat "${file}.SKIPPED.txt")"
    continue
  fi
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
