import test from "node:test";
import assert from "node:assert/strict";
import { bridgeForChair, counterText, isPlaying, mapName, statusTitle, verificationText, withheldText } from "../src/lib/bridgeStatus.js";

const state = {
    documents: [{ hash: "s", name: "Strings r4" }],
    sections: [{ key: "strings", label: "Orchestral strings", hash: "s" }],
    assignments: [{ port: 1, channel: 3, hash: "s", verified: false, beacon: "absent", receivedBeacon: [-1, -1], expectedBeacon: [9, 8] }],
};

test("bridge assignments use 1-based channels; chairs use 0-based", () => {
    assert.equal(bridgeForChair(state, { port: 1, channel: 2 }).label, "Orchestral strings");
    assert.equal(bridgeForChair(state, { port: 1, channel: 3 }), null);
    assert.equal(bridgeForChair({}, { port: 0, channel: 0 }), null);
});

test("the map is named once", () => {
    assert.equal(mapName({ label: "Orchestral strings" }), "Orchestral strings map");
    assert.equal(mapName({ label: "Bridge map" }), "Bridge map");
});

test("status text distinguishes version check, attestation and mismatch in plain words", () => {
    const a = state.assignments[0];
    assert.equal(verificationText(a), "Play to check the version");
    assert.equal(isPlaying(a), false);
    assert.equal(verificationText({ ...a, beacon: "matched" }), "✓ Dorico has this version");
    assert.equal(isPlaying({ ...a, beacon: "matched" }), true);
    assert.equal(verificationText({ ...a, beacon: "unsupported", verified: true }), "✓ Confirmed from an export");
    assert.equal(verificationText({ ...a, beacon: "unsupported" }), "Not checked · its notes are muted");
    assert.equal(isPlaying({ ...a, beacon: "mismatched", verified: true }), false);
    const mismatched = { ...a, beacon: "mismatched", receivedBeacon: [1, 2] };
    assert.equal(verificationText(mismatched), "Dorico has a different version · its notes are muted");
    // The numbers and the fix are in the tooltip, not the row.
    assert.match(statusTitle(mismatched), /version ID 1\/2, but Fiddle installed 9\/8/);
    assert.match(statusTitle(mismatched), /Install Playback Template/);
    assert.match(statusTitle({ ...a, beacon: "matched" }), /keyswitches 126\/127/);
    for (const text of [verificationText(a), verificationText(mismatched), statusTitle(mismatched)])
        assert.doesNotMatch(text, /beacon|withheld/i, "no internal jargon");
});

test("notes not played say why", () => {
    const playing = { ...state.assignments[0], beacon: "matched" };
    assert.equal(withheldText(playing), "");
    assert.equal(withheldText({ ...playing, withheldNotes: 2, withheldByReason: { outsideRange: 2 } }),
        " · 2 notes not played (outside the instrument's range)");
    assert.equal(withheldText({ ...playing, withheldNotes: 1, lastWithheldReason: "baseUnknown" }),
        " · 1 note not played (arrived before the starting articulation)");
    const mixed = { ...playing, withheldNotes: 4, withheldByReason: { baseUnknown: 1, outsideRange: 3 } };
    assert.equal(withheldText(mixed), " · 4 notes not played");
    assert.match(statusTitle(mixed), /Notes not played since the maps loaded: 3 outside the instrument's range; 1 arrived before/);
    // A muted map's status already says why.
    const muted = { ...playing, beacon: "mismatched", receivedBeacon: [1, 2], withheldNotes: 2, withheldByReason: { beaconMismatch: 2 } };
    assert.equal(withheldText(muted), " · 2 notes not played");
    assert.equal(counterText({ withheldNotes: 2, debouncedOffs: 1 }), "1 early Offs suppressed");
});
