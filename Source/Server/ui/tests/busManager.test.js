import assert from "node:assert/strict";
import { readFileSync, existsSync } from "node:fs";
import test from "node:test";

const read = (name) => readFileSync(new URL(`../src/lib/${name}`, import.meta.url), "utf8");
const mixerSource = read("MixerPanel.svelte");
const bankSource = read("AudioBusBank.svelte");
const addSource = read("AddBusDialog.svelte");
const detailsSource = read("BusDetailsPanel.svelte");
const layerDetailsSource = read("LayerDetailsPanel.svelte");

test("buses are added from the bank header with a name dialog", () => {
    assert.ok(!existsSync(new URL("../src/lib/BusManager.svelte", import.meta.url)));
    assert.doesNotMatch(mixerSource, /bus-manager/);
    assert.doesNotMatch(bankSource, />Manage</);
    assert.match(bankSource, /event\.stopPropagation\(\); onAddBus\(\);/);
    assert.match(mixerSource, /<AddBusDialog/);
    assert.match(mixerSource, /dispatchCpp\("addGroupBus", name, JSON\.stringify\(routeSelection \? selectedLayerIds\(\) : \[\]\)\)/);
    assert.match(addSource, /Route the \{selectedCount\} selected/);
    assert.match(addSource, /already exists/);
});

test("each bus has Edit details… for rename and delete", () => {
    assert.match(bankSource, /onEditBus\(bus\.id\)[^>]*>Edit details…</);
    assert.match(mixerSource, /<BusDetailsPanel/);
    assert.match(mixerSource, /dispatchCpp\("renameGroupBus", detailsBus\.id, name\)/);
    assert.match(mixerSource, /dispatchCpp\("removeGroupBus", detailsBus\.id\)/);
    assert.match(detailsSource, /Delete bus/);
    assert.match(detailsSource, /Undo restores/);
    assert.doesNotMatch(detailsSource, /window\.confirm/);
});

test("direct-output routing is visible on every mixer strip", () => {
    assert.doesNotMatch(layerDetailsSource, /Audio output/);
    assert.match(mixerSource, /ch-output-routing/);
    assert.match(mixerSource, /Audio output for/);
    assert.match(mixerSource, /setStripDirectOutput/);
    assert.match(mixerSource, /setGroupStripDirectOutput/);
});
