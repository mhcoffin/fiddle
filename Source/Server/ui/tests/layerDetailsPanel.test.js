import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const mixerSource = readFileSync(
    new URL("../src/lib/MixerPanel.svelte", import.meta.url),
    "utf8",
);
const detailsSource = readFileSync(
    new URL("../src/lib/LayerDetailsPanel.svelte", import.meta.url),
    "utf8",
);

test("every strip reserves the same operational space around its fader", () => {
    assert.match(mixerSource, /--strip-pre-fader-height/);
    assert.match(mixerSource, /height: var\(--strip-pre-fader-height/);
    assert.match(mixerSource, /\.ch-fx-zone\s*\{[\s\S]*?height: 180px/);
    assert.match(mixerSource, /\.master-strip-bot-spacer\s*\{[\s\S]*?height: 74px/);
});

test("rarely changed layer configuration lives in the details panel", () => {
    assert.match(mixerSource, /<LayerDetailsPanel/);
    assert.match(mixerSource, /"Edit details…"/);
    assert.match(detailsSource, /VST instrument/);
    assert.match(detailsSource, /Expression map/);
    assert.match(detailsSource, /Lua processors/);
    assert.match(detailsSource, /Inspect MIDI/);
    assert.match(detailsSource, /Refresh Library/);
    assert.doesNotMatch(detailsSource, /Audio output/);
});

test("operational instrument and effect controls remain on large strips", () => {
    assert.match(mixerSource, /ch-instrument-summary/);
    assert.match(mixerSource, /ch-fx-summary-actions/);
    assert.match(mixerSource, /ch-output-routing/);
    assert.match(mixerSource, /setStripInsertBypassed/);
    assert.match(mixerSource, /toggleStripInsertEditor/);
});

test("missing instruments can be retried without discarding their saved state", () => {
    assert.match(mixerSource, />Retry VSTi<\/button>/);
    assert.match(mixerSource, /setPlugin\(strip\.id, strip\.pluginUid\)/);
    assert.match(mixerSource, /strip\.pluginStatus === "missing"/);
    assert.match(detailsSource, />Retry instrument<\/button>/);
    assert.match(detailsSource, /strip\.pluginError/);
});
