#!/bin/sh
set -e

cd "$(dirname "$0")/.."

CONFIG=${1:-Debug}

sh tools/build.sh $CONFIG

echo "running..."
./build/Mallow