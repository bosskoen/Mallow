#!/bin/sh
set -e

cd "$(dirname "$0")/.."

echo "[1/6] configuring test_runner..."
cmake -S tools/test_runner -B tools/test_runner/build -DCMAKE_BUILD_TYPE=Release

echo "[2/6] building test_runner..."
cmake --build tools/test_runner/build --config Release

echo "[3/6] running test_runner..."
./tools/test_runner/build/test_runner

echo "[4/6] configuring generated tests..."
cmake -S generated/tests -B generated/tests/build

echo "[5/6] building generated tests..."
cmake --build generated/tests/build --config Release

echo "[6/6] running tests..."
./generated/tests/build/all_tests