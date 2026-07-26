#!/bin/sh
set -e

cd "$(dirname "$0")/.."

echo "cleaning build..."
rm -rf build

echo "cleaning generated..."
rm -rf generated


echo "done"