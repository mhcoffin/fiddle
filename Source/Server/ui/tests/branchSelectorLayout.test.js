import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const mixerSource = readFileSync(
    new URL("../src/lib/MixerPanel.svelte", import.meta.url),
    "utf8",
);
const branchSource = readFileSync(
    new URL("../src/lib/BranchSelector.svelte", import.meta.url),
    "utf8",
);

test("the mixer toolbar does not clip branch popovers", () => {
    const toolbarRule = mixerSource.match(/\.mixer-toolbar\s*\{[\s\S]*?\n\s*\}/)?.[0];
    assert.ok(toolbarRule, "mixer toolbar CSS rule should exist");
    assert.match(toolbarRule, /overflow:\s*visible/);
    assert.doesNotMatch(toolbarRule, /overflow-x:\s*auto/);
});

test("the active branch compares stable branch IDs", () => {
    assert.match(branchSource, /class:active=\{branch\.id === currentBranch\}/);
});
