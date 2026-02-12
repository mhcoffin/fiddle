#!/bin/bash
set -e

echo "===== Rebuilding Svelte UI ====="
cd Source/Server/ui
rm -rf node_modules dist
echo "Installing dependencies..."
npm install
echo "Building UI..."
npm run build
cd ../../..

echo "===== Rebuilding C++ Backend ====="
rm -rf build
mkdir build
cd build
echo "Configuring CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release
echo "Building Project..."
cmake --build . --config Release

echo "===== DONE! Everything is rebuilt. ====="
