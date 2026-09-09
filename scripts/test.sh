#!/usr/bin/env bash
set -euo pipefail

# Run from any working directory. Keep build artefacts between runs so routine
# regression testing is incremental. No server launch or plug-in installation.
test_repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_build="${FIDDLE_TEST_BUILD_DIR:-$test_repo/build}"
test_jobs="${FIDDLE_TEST_JOBS:-3}"

cmake -S "$test_repo" -B "$test_build"
cmake --build "$test_build" --config Release --parallel "$test_jobs" --target FiddleTests
ctest --test-dir "$test_build" -C Release --output-on-failure \
  --no-tests=error --output-junit "$test_build/stable-tests.xml" -L stable "$@"
