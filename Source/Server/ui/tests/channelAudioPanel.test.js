import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const mixerSource = readFileSync(
    new URL("../src/lib/MixerPanel.svelte", import.meta.url),
    "utf8",
);
const panelSource = readFileSync(
    new URL("../src/lib/ChannelAudioPanel.svelte", import.meta.url),
    "utf8",
);

test("instrument strips expose one spacious Audio FX panel", () => {
    assert.match(mixerSource, />Audio FX\{/);
    assert.match(mixerSource, /requestStripAudioState/);
    assert.match(mixerSource, /<ChannelAudioPanel/);
});

test("large strips show effect names with direct edit and bypass controls", () => {
    assert.match(mixerSource, /stripSize === "large"/);
    assert.match(mixerSource, /ch-fx-summary-name/);
    assert.match(mixerSource, /setStripInsertBypassed/);
    assert.match(mixerSource, /toggleStripInsertEditor/);
    assert.match(mixerSource, /insert\.editorOpen \? "Hide" : "Edit"/);
});

test("channel audio separates pre and post-fader racks", () => {
    assert.match(panelSource, /Pre-fader inserts/);
    assert.match(panelSource, /Post-fader inserts/);
    assert.match(panelSource, /addStripInsert/);
    assert.match(panelSource, /moveStripInsert/);
    assert.match(panelSource, /setStripInsertBypassed/);
    assert.match(panelSource, /showStripInsertEditor/);
    assert.match(panelSource, /removeStripInsert/);
});
