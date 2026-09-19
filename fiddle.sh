#!/usr/bin/env bash
set -euo pipefail

# One canonical app location, independent of the caller's working directory.
fiddle_repo="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fiddle_build="$fiddle_repo/build/release"
fiddle_app="$fiddle_build/FiddleServer_artefacts/Release/FiddleServer.app"

case "${1:-open}" in
  open)
    if [[ ! -d "$fiddle_app" ]]; then
      echo "Fiddle has not been built here. Run: $fiddle_repo/fiddle.sh build" >&2
      exit 1
    fi
    echo "Opening $fiddle_app"
    echo "If Fiddle is already running, quit it normally first to load a rebuilt version."
    open "$fiddle_app"
    ;;
  build)
    if [[ ! -f "$fiddle_build/CMakeCache.txt" ]]; then
      cmake -S "$fiddle_repo" -B "$fiddle_build" -DCMAKE_BUILD_TYPE=Release
    fi
    cmake --build "$fiddle_build" --config Release --parallel "${FIDDLE_BUILD_JOBS:-3}" --target FiddleServer
    echo "Built $fiddle_app (not launched)."
    ;;
  path)
    printf '%s\n' "$fiddle_app"
    ;;
  reveal)
    open -R "$fiddle_app"
    ;;
  help|-h|--help)
    echo "Usage: $0 [open|build|path|reveal]"
    echo "Defaults to opening the existing Release app; build compiles without launching."
    echo "Never kills Fiddle, changes versions, or installs the Dorico plugin."
    ;;
  *)
    echo "Unknown command: $1. Use --help." >&2
    exit 2
    ;;
esac
