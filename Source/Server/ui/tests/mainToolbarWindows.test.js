import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const mixerSource = readFileSync(
    new URL("../src/lib/MixerPanel.svelte", import.meta.url),
    "utf8",
);

test("the main toolbar can show or focus the Library Manager", () => {
    assert.match(mixerSource, />\s*Library Manager\s*<\/button>/);
    assert.match(mixerSource, /dispatchCpp\("showLibraryManagerWindow"\)/);
});
