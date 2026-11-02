import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

test("bridge maps offer the built-in revision when a section's pin differs", () => {
    const pane = readFileSync(new URL("../src/lib/BridgeMaps.svelte", import.meta.url), "utf8");
    assert.match(pane, /dispatchCpp\("useBuiltInBridgeMaps"/);
    assert.match(pane, /section\.updateAvailable/);
    assert.match(pane, /Use built-in revision \{section\.builtInRevision\}/);
    assert.match(pane, /Install Playback Template/);
    const handlers = readFileSync(new URL("../../BridgeMapUi.cpp", import.meta.url), "utf8");
    assert.match(handlers, /registerHandler\("useBuiltInBridgeMaps"/);
    assert.match(handlers, /setBuiltIns\(/);
    const commands = readFileSync(new URL("../../BridgeMapCommands.h", import.meta.url), "utf8");
    assert.match(commands, /setProperty\("updateAvailable"/);
});
