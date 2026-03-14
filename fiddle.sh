#!/bin/bash
set -e

# Fiddle Server Build and Launch Script
# This script builds the UI, builds the server, and launches it

echo "🎻 Fiddle Server Build & Launch"
echo "================================"

# Step 1: Build the UI
echo ""
echo "📦 Building Svelte UI..."
cd Source/Server/ui
pnpm run build
cd ../../..

# Step 2: Build the Plugin
echo ""
echo "🔌 Building Fiddle Plugin (VST3)..."
./scripts/bump_version.sh
cmake --build build --target FiddleNative

# Step 3: Build the Server
echo ""
echo "🔨 Building FiddleServer..."
# Force clean the app bundle to ensure UI resources are updated
rm -rf build/FiddleServer_artefacts/Debug/FiddleServer.app
cmake --build build --target FiddleServer

# Step 4: Install Plugin to system directory
echo ""
echo "📦 Installing Fiddle Plugin to ~/Library/Audio/Plug-Ins/VST3/..."
rm -rf ~/Library/Audio/Plug-Ins/VST3/Fiddle.vst3

# Check for VST3 in Debug folder first (CMake default on Mac for some generators)
if [ -d "build/FiddleNative_artefacts/Debug/VST3/Fiddle.vst3" ]; then
    cp -R build/FiddleNative_artefacts/Debug/VST3/Fiddle.vst3 ~/Library/Audio/Plug-Ins/VST3/
elif [ -d "build/FiddleNative_artefacts/VST3/Fiddle.vst3" ]; then
    cp -R build/FiddleNative_artefacts/VST3/Fiddle.vst3 ~/Library/Audio/Plug-Ins/VST3/
else
    echo "❌ Error: Could not find built Fiddle.vst3!"
    exit 1
fi

# Step 5: Kill any running FiddleServer
echo ""
echo "🛑 Stopping any running FiddleServer..."
killall FiddleServer 2>/dev/null || echo "   (No running FiddleServer found)"

# Step 6: Launch the new FiddleServer
echo ""
echo "🚀 Launching FiddleServer..."
open build/FiddleServer_artefacts/Debug/FiddleServer.app

echo ""
echo "✅ Done! FiddleServer is starting..."
echo "   Plugin installed to ~/Library/Audio/Plug-Ins/VST3/"
echo "   Make sure to reload the Fiddle plugin in Dorico!"
echo "   Check the Timeline tab to see instrument names!"
