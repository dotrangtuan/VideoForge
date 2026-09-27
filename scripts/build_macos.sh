#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

QT_PREFIX="$(brew --prefix qt)"
rm -rf build-mac

cmake -S . -B build-mac -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build build-mac -j

echo
echo "Build complete."
echo "Run: open build-mac/VideoForgeEditor.app"
