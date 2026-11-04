import test from "node:test";
import assert from "node:assert/strict";
import { bridgeForChair, counterText, isPlaying, verificationText } from "../src/lib/bridgeStatus.js";

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

test("status text distinguishes beacon, attestation and mismatch", () => {
    const a = state.assignments[0];
    assert.match(verificationText(a), /Waiting for the revision beacon/);
    assert.equal(isPlaying(a), false);
    assert.equal(verificationText({ ...a, beacon: "matched" }), "Beacon matches");
    assert.equal(isPlaying({ ...a, beacon: "matched" }), true);
    assert.equal(isPlaying({ ...a, beacon: "mismatched", verified: true }), false);
    assert.match(verificationText({ ...a, beacon: "mismatched", receivedBeacon: [1, 2] }), /beacon 1\/2, expected 9\/8/);
    assert.equal(counterText({ withheldNotes: 2, debouncedOffs: 1 }), "2 notes withheld · 1 early Offs suppressed");
});
