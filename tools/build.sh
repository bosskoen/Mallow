#!/bin/sh
set -e

cd "$(dirname "$0")/.."

CONFIG=${1:-Debug}

echo "[1/2] configuring..."
cmake -S . -B build

echo "[2/2] building..."
cmake --build build --config $CONFIG