import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const mixerSource = readFileSync(
    new URL("../src/lib/MixerPanel.svelte", import.meta.url),
    "utf8",
);
const panelSource = readFileSync(
    new URL("../src/lib/BusAudioPanel.svelte", import.meta.url),
    "utf8",
);
const bankSource = readFileSync(
    new URL("../src/lib/AudioBusBank.svelte", import.meta.url),
    "utf8",
);

test("mixer exposes a distinct metered audio bus bank", () => {
    assert.match(mixerSource, /<AudioBusBank/);
    assert.match(bankSource, /class="bank"/);
    assert.match(bankSource, /routeCount/);
    assert.match(bankSource, /setGroupBusGain/);
    assert.match(bankSource, /setGroupBusInsertBypassed/);
    assert.match(bankSource, /toggleGroupBusInsertEditor/);
    assert.match(bankSource, /setMasterInsertBypassed/);
    assert.match(bankSource, /toggleMasterInsertEditor/);
    assert.match(mixerSource, /<BusAudioPanel/);
    assert.match(bankSource, /let collapsed/);
    assert.match(bankSource, /class="header-toggle"/);
    assert.match(bankSource, /aria-expanded/);
    assert.match(bankSource, />Master</);
    assert.match(bankSource, /padding-bottom: 21px/);
    assert.match(bankSource, /height: calc\(var\(--strip-pre-fader-height, 166px\) \+ 2px\)/);
});

test("bus audio panel supports complete pre and post fader rack editing", () => {
    assert.match(panelSource, /preFader/);
    assert.match(panelSource, /postFader/);
    assert.match(panelSource, /addGroupBusInsert/);
    assert.match(panelSource, /moveGroupBusInsert/);
    assert.match(panelSource, /setGroupBusInsertBypassed/);
    assert.match(panelSource, /toggleGroupBusInsertEditor/);
    assert.match(panelSource, /removeGroupBusInsert/);
});
