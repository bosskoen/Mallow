#!/bin/sh
set -e
cd "$(dirname "$0")/.."

CONFIG=${1:-Release}

echo "[1/3] configuring ($CONFIG)..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=$CONFIG
echo "[2/3] building tests..."
cmake --build build --target tests --config $CONFIG
echo "[3/3] running tests..."
./build/tests/tests