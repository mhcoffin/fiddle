import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const mixerSource = readFileSync(
    new URL("../src/lib/MixerPanel.svelte", import.meta.url), "utf8",
);
const managerSource = readFileSync(
    new URL("../src/lib/BusManager.svelte", import.meta.url), "utf8",
);
const detailsSource = readFileSync(
    new URL("../src/lib/LayerDetailsPanel.svelte", import.meta.url), "utf8",
);

test("the toolbar opens a spacious Bus Manager", () => {
    assert.match(mixerSource, /<BusManager/);
    assert.match(mixerSource, /Audio Buses/);
    assert.match(mixerSource, /requestGroupBusState/);
    assert.match(managerSource, /Create from selection/);
    assert.match(managerSource, /addGroupBus/);
    assert.match(managerSource, /removeGroupBus/);
    assert.match(managerSource, /Confirm remove/);
    assert.doesNotMatch(managerSource, /window\.confirm/);
});

test("bus management exposes naming, order, gain, mute, and solo", () => {
    assert.match(managerSource, /renameGroupBus/);
    assert.match(managerSource, /moveGroupBus/);
    assert.match(managerSource, /setGroupBusGain/);
    assert.match(managerSource, /setGroupBusMute/);
    assert.match(managerSource, /setGroupBusSolo/);
});

test("direct-output routing is visible on every mixer strip", () => {
    assert.doesNotMatch(detailsSource, /Audio output/);
    assert.match(mixerSource, /ch-output-routing/);
    assert.match(mixerSource, /Audio output for/);
    assert.match(mixerSource, /setStripDirectOutput/);
    assert.match(mixerSource, /setGroupStripDirectOutput/);
});
