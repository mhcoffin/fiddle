import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const mixerSource = readFileSync(
    new URL("../src/lib/MixerPanel.svelte", import.meta.url),
    "utf8",
);

test("the main toolbar exposes one directory for workspaces, setup and diagnostics", () => {
    assert.match(mixerSource, />\s*Library Manager\s*<\/button>/);
    assert.match(mixerSource, /dispatchCpp\("showView", "library-manager"\)/);
    assert.match(mixerSource, />\s*Project History\s*<\/button>/);
    assert.match(mixerSource, />\s*Audio Performance\s*<\/button>/);
    assert.match(mixerSource, />\s*MIDI Capture\s*<\/button>/);
    assert.match(mixerSource, />\s*Settings…\s*<\/button>/);
    assert.match(mixerSource, /dispatchCpp\("showView", "settings"\)/);
    assert.doesNotMatch(mixerSource, /<ToolbarMenu label="Setup">/);
});
