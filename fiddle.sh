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
  restart)
    # Same as Fiddle's own Restart Fiddle (ApplicationRestart.cpp): quit
    # normally, then relaunch in the background with --relaunched. Fiddle
    # waits in the Stage Manager strip, and when brought forward its windows
    # return to their saved places. A plain `open` brings Fiddle forward at
    # once, so Stage Manager's placement would be saved as the windows' places.
    if [[ ! -d "$fiddle_app" ]]; then
      echo "Fiddle has not been built here. Run: $fiddle_repo/fiddle.sh build" >&2
      exit 1
    fi
    fiddle_executable="$fiddle_app/Contents/MacOS/FiddleServer"
    if pgrep -f "$fiddle_executable" >/dev/null; then
      echo "Quitting Fiddle..."
      osascript -e "tell application \"$fiddle_app\" to quit" >/dev/null
      for _ in $(seq 1 300); do
        pgrep -f "$fiddle_executable" >/dev/null || break
        sleep 0.1
      done
      if pgrep -f "$fiddle_executable" >/dev/null; then
        echo "Fiddle is still running (perhaps waiting on a dialog); not relaunching." >&2
        exit 1
      fi
    fi
    echo "Relaunching $fiddle_app in the background, as Restart Fiddle does."
    open -g -n "$fiddle_app" --args --relaunched
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
    echo "Usage: $0 [open|restart|build|path|reveal]"
    echo "Defaults to opening the existing Release app; build compiles without launching."
    echo "restart quits Fiddle normally and relaunches it in the background, like Restart Fiddle."
    echo "Never kills Fiddle, changes versions, or installs the Dorico plugin."
    ;;
  *)
    echo "Unknown command: $1. Use --help." >&2
    exit 2
    ;;
esac
