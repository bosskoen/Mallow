#!/usr/bin/env bash
# docs.sh -- generate API docs into build/docs/html
# Run from the repo root:  ./docs.sh
set -euo pipefail
 
# Configure the build dir once, only if it isn't set up yet.
if [ ! -f "build/CMakeCache.txt" ]; then
    cmake -B build -S .
fi
 
# Build only the docs target (skips compiling the rest).
cmake --build build --target docs
 
INDEX="build/docs/html/index.html"
echo
echo "Docs generated: $INDEX"
 
# Open in the default browser (Linux uses xdg-open, macOS uses open).
if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$INDEX" >/dev/null 2>&1 || true
elif command -v open >/dev/null 2>&1; then
    open "$INDEX" || true
fi
 