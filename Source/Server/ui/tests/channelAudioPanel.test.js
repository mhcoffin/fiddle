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
    assert.match(mixerSource, /\/ Sends\$\{strip.sends/);
    assert.match(mixerSource, /requestStripAudioState/);
    assert.match(mixerSource, /<ChannelAudioPanel/);
});

test("channel audio exposes post-fader sends with destinations and levels", () => {
    assert.match(panelSource, /Post-fader sends/);
    for (const command of ["addStripSend", "updateStripSend", "removeStripSend"])
        assert.ok(panelSource.includes(command));
    assert.match(panelSource, /<Knob label="Send level" unit="dB"/);
    assert.match(panelSource, /Show on strip/);
    assert.match(panelSource, /setStripSendVisible/);
    assert.match(mixerSource, /<PromotedSends/);
    assert.match(mixerSource, /buses=\{groupBuses\}/);
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
