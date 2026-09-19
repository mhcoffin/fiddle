import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { dispatchDebugBatch, isDebugMessage } from "../src/lib/debugUiFeed.js";

test("diagnostic batches preserve order, bound work, and cannot deliver control state", () => {
    const received = []; let resets = 0; let lost = 0;
    const callbacks = { reset: () => resets++, dispatch: m => received.push(m), dropped: n => lost += n };
    dispatchDebugBatch({ reset: true, dropped: 12, messages: [
        { type: "updateNoteState", data: "start" }, { type: "pushMidiEvent", data: "stop" },
        { type: "setMixerState", data: [] }, { type: "setDebugBatch", data: {} },
    ] }, callbacks);
    assert.equal(resets, 1); assert.equal(lost, 12);
    assert.deepEqual(received.map(m => m.data), ["start", "stop"]);
    received.length = 0;
    dispatchDebugBatch({ messages: Array.from({length:1000}, (_,data) => ({type:"pushMidiEvent", data})) }, callbacks);
    assert.equal(received.length, 128); assert.equal(received[0].data, 872);
    dispatchDebugBatch(null, callbacks);
});

test("main, library, and history IPC ignore debug floods, not meters or control messages", async () => {
    for (const search of ["", "?view=library", "?view=history", "?mode=debug"]) {
        globalThis.window = { location: { search } };
        const { onFromCpp } = await import(`../src/lib/ipc.js?test=${encodeURIComponent(search)}`);
        let events = 0, meters = 0;
        onFromCpp("pushMidiEvent", () => events++);
        onFromCpp("setDebugBatch", () => events++);
        onFromCpp("setMixerMeters", () => meters++);
        for (let i=0; i<10000; ++i) window.__dispatchFromCpp({type:"pushMidiEvent",data:{}});
        window.__dispatchFromCpp({type:"setDebugBatch",data:{}});
        window.__dispatchFromCpp({type:"setMixerMeters",data:{}});
        assert.equal(events, search === "?mode=debug" ? 10001 : 0);
        assert.equal(meters, 1);
    }
    delete globalThis.window;
});

test("native routing classifies the same diagnostic stream and never broadcasts its batches", () => {
    const native = readFileSync(new URL("../../DebugUiFeed.h", import.meta.url), "utf8");
    for (const name of ["pushMidiEvent", "updateNoteState", "addLogMessage", "metronomeBeat", "setTempo"]) {
        assert.ok(isDebugMessage(name)); assert.ok(native.includes(`type == "${name}"`));
    }
    const main = readFileSync(new URL("../../MainComponent.cpp", import.meta.url), "utf8");
    assert.match(main, /if \(DebugUiFeed::accepts\(type\)\) \{\s*debugUiFeed_\.push\(type, data\);\s*return;/);
    assert.match(main, /debugWindow_->getWebView\(\)\.evaluateJavascript\(/);
    assert.doesNotMatch(main, /broadcastMessage\("(?:pushMidiEvent|updateNoteState|addLogMessage)"/);
    const app = readFileSync(new URL("../src/App.svelte", import.meta.url), "utf8");
    assert.match(app, /let midiEvents = \$state.raw\(\[\]\)/);
    assert.match(app, /slice\(-debugHistoryLimit\)/);
});
