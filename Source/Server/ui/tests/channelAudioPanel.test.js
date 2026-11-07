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
const busPanelSource = readFileSync(
    new URL("../src/lib/BusAudioPanel.svelte", import.meta.url),
    "utf8",
);

test("instrument strips expose one spacious Audio FX panel", () => {
    assert.match(mixerSource, /Manage \$\{location.toLowerCase\(\)\}-fader effects — /);
    assert.match(mixerSource, /Manage \$\{chairLabel\} chair \$\{location.toLowerCase\(\)\}-fader effects and sends/);
    assert.match(mixerSource, /requestStripAudioState/);
    assert.match(mixerSource, /<ChannelAudioPanel/);
});

test("chair audio has the pre/post-fader sends; layers have none", () => {
    assert.match(busPanelSource, /aria-label="Send tap"/);
    assert.match(busPanelSource, /<option value="preFader">Pre-fader/);
    assert.match(busPanelSource, /<option value="postFader">Post-fader/);
    for (const command of ["addChairSend", "updateChairSend", "removeChairSend", "setChairSendPreFader", "setChairSendVisible"])
        assert.ok(busPanelSource.includes(command));
    assert.match(busPanelSource, /<Knob label="Send level" unit="dB"/);
    assert.match(busPanelSource, /Show on strip/);
    assert.match(busPanelSource, /\{#if isChair\}/);
    assert.doesNotMatch(panelSource, /Send/);
    assert.match(mixerSource, /<PromotedSends chair=\{chairBus\}/);
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
