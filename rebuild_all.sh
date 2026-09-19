#!/usr/bin/env bash
set -euo pipefail

# Incremental full build. Never remove shared dependencies or other build trees.
fiddle_repo="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fiddle_build="$fiddle_repo/build/release"
cmake -S "$fiddle_repo" -B "$fiddle_build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$fiddle_build" --config Release --parallel "${FIDDLE_BUILD_JOBS:-3}"
echo "Release build complete. No app was restarted and no plugin was installed."
