#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEX_DIR="$ROOT_DIR/tests/pdf"

if ! command -v pdflatex &> /dev/null; then
  echo "pdflatex not found. Install TeX Live or similar." >&2
  exit 1
fi

cd "$TEX_DIR"

for tex in *.tex; do
  if [[ ! -f "$tex" ]]; then
    continue
  fi
  echo "Compiling $tex..."
  base="${tex%.tex}"
  pdflatex -interaction=nonstopmode -jobname="$base" \
    "\\pdfminorversion=4\\pdfobjcompresslevel=0\\input{$tex}" > /dev/null 2>&1 || {
    echo "Failed to compile $tex" >&2
    exit 1
  }
done

# Clean up auxiliary files
rm -f *.aux *.log

echo "PDFs generated successfully in $TEX_DIR"
ls -lh *.pdf
