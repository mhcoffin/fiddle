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
    assert.match(mixerSource, /--strip-pre-fader-height/); // the bus bank mirrors it
    assert.match(mixerSource, /\.ch-fx-zone\s*\{[\s\S]*?height: calc\(var\(--strip-fx-height\).*var\(--strip-send-span/);
    // The chair strip's Σ readout takes the -∞ tick's line and its lock sits
    // beside the level, so chair and layer faders align.
    assert.match(mixerSource, /fader-tick fader-bot blend-level/);
    assert.match(mixerSource, /class="ch-chair-gain-row"/);
    assert.match(mixerSource, /\.ch-chair-output\s*\{[\s\S]*?height: 32px/);
    assert.doesNotMatch(mixerSource, /master-strip/);
});

test("strip effects are grouped by signal position; only chairs have an output", () => {
    assert.match(mixerSource, /\["Pre", "Post"\]/);
    assert.match(mixerSource, /insert\.location === location/);
    assert.match(mixerSource, /sendRows\[location\] > 0/);
    assert.match(mixerSource, /preFader=\{location === "Pre"\}/);
    assert.ok(mixerSource.indexOf("<PromotedSends chair={chairBus}") < mixerSource.indexOf('class="ch-chair-output"'));
    assert.doesNotMatch(mixerSource, /ch-output-routing/);
    assert.match(mixerSource, /horizontal=\{stripSize === "large"\}/);
    assert.match(mixerSource, /\.ch-strip-section\s*\{[^}]*background: #1b283b/);
    // The VSTi control needs no "Instrument" heading (the section keeps its accessible name).
    assert.doesNotMatch(mixerSource, /ch-section-title">Instrument/);
    assert.match(mixerSource, /ch-instrument-section" aria-label="Instrument"/);
    assert.doesNotMatch(mixerSource, /ch-section-title">Output/);
    assert.match(mixerSource, /ch-section-title">\{location\}-fader/);
    assert.match(mixerSource, /ch-fx-empty">No inserts/);
});

test("rarely changed layer configuration lives in the details panel", () => {
    assert.match(mixerSource, /<LayerDetailsPanel/);
    assert.match(mixerSource, /"Edit details…"/);
    assert.match(detailsSource, /VST instrument/);
    assert.match(detailsSource, /Expression map/);
    assert.match(detailsSource, /Lua processors/);
    assert.match(detailsSource, /Inspect MIDI/);
    assert.match(detailsSource, /Update from Library/);
    assert.doesNotMatch(detailsSource, /Audio output/);
});

test("section headings count inserts independently of sends and show active status", () => {
    assert.match(mixerSource, /if inserts.length > 0\}<span class="ch-insert-count"/);
    assert.match(mixerSource, /class:active=\{activeInserts > 0\}/);
    assert.match(mixerSource, /!insert.bypassed && insert.status === "loaded"/);
    assert.match(mixerSource, /stripSize === "compact" \? "" : " FX"/);
    assert.match(mixerSource, /aria-label=\{`Manage[^\n]*insertSummary/);
});

test("operational instrument and effect controls remain on large strips", () => {
    assert.match(mixerSource, /ch-instrument-summary/);
    assert.match(mixerSource, /ch-fx-summary-actions/);
    assert.match(mixerSource, /class="ch-chair-output"/);
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
