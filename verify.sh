#!/usr/bin/env bash
# verify.sh — fast feedback loop for sappsounds.
# Configure (first run), build library + tests + tools, run the unit suite.

set -e
cd "$(dirname "$0")"

echo "▶ configure"
if [ ! -d build ]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSAPPSOUNDS_BUILD_TESTS=ON -DSAPPSOUNDS_BUILD_TOOLS=ON > /dev/null
fi

echo "▶ build"
cmake --build build -j8 2>&1 | grep -E "error|FAILED" && exit 1 || true

echo "▶ tests"
./build/SappSoundsTests --reporter compact | tail -2

echo "✓ verify passed"
