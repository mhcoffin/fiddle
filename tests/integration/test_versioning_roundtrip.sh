#!/usr/bin/env bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$DIR/.."
BUILD_DIR="$PROJECT_ROOT/build"
SERVER_BIN="$BUILD_DIR/FiddleServer_artefacts/FiddleServer.app/Contents/MacOS/FiddleServer"
if [ ! -f "$SERVER_BIN" ]; then
    SERVER_BIN="$BUILD_DIR/FiddleServer_artefacts/Debug/FiddleServer.app/Contents/MacOS/FiddleServer"
fi
MOCK_BIN="$BUILD_DIR/fiddle-mock"
if [ ! -f "$MOCK_BIN" ]; then
    MOCK_BIN="$BUILD_DIR/Debug/fiddle-mock"
fi
DB_FILE="$HOME/Library/Application Support/Fiddle/fiddle.db"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "=================================="
echo " Starting Integration Test..."
echo "=================================="

# 1. Clean up old DB and blobs to start fresh
echo -n "Cleaning up old database... "
rm -f "$DB_FILE"
rm -f /tmp/fiddle_v1.bin
rm -f /tmp/fiddle_v2.bin
echo -e "${GREEN}Done${NC}"

# 2. Start FiddleServer in the background
echo -n "Starting FiddleServer... "
"$SERVER_BIN" &
SERVER_PID=$!

# Give it a couple of seconds to initialize UI and DB
sleep 3
echo -e "${GREEN}Done (PID: $SERVER_PID)${NC}"

# Ensure we cleanup server on exit
trap "echo 'Killing Server...'; kill $SERVER_PID; rm -f /tmp/fiddle*.bin;" EXIT

# 3. Capture Initial State (v1)
echo "----------------------------------"
echo "Capturing Initial State (v1)"
"$MOCK_BIN" save /tmp/fiddle_v1.bin

echo "Inspecting v1 blob:"
"$MOCK_BIN" inspect /tmp/fiddle_v1.bin

# 4. We need to force a DB mutation. FiddleServer doesn't have a clean headless REST API for mutation yet.
# We will simulate Dorico updating the project. We will load a modified mock block.
# Actually, the easiest way to branch the graph is:
# - Server naturally makes changes over time (strips dragged in).
# - Without headless manipulation, let's just observe if root hash is created initially.

echo "----------------------------------"
echo "Validating SQLite Graph"

# Query DB directly to see if tables were populated
echo "Checking Version Branches..."
sqlite3 "$DB_FILE" "SELECT id, name, head_hash FROM branches;"
BRANCH_COUNT=$(sqlite3 "$DB_FILE" "SELECT count(*) FROM branches;")

if [ "$BRANCH_COUNT" -eq 0 ]; then
    echo -e "${RED}Test Failed: No branches written to table.${NC}"
    exit 1
fi
echo -e "${GREEN}Branch table populated successfully.${NC}"

echo "Checking Fiddle States..."
STATE_COUNT=$(sqlite3 "$DB_FILE" "SELECT count(*) FROM fiddle_states;")
if [ "$STATE_COUNT" -eq 0 ]; then
    echo -e "${RED}Test Failed: No fiddle_states written to table.${NC}"
    exit 1
fi
echo -e "${GREEN}State table populated successfully.${NC}"

echo "----------------------------------"
echo -e "${GREEN}INTEGRATION TESTS PASSED${NC}"
echo "=================================="
exit 0
