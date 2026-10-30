import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import { formatLength, instrumentLabel, normalizeState, recordingStatus, runHeadline } from "../src/lib/scoreTests.js";

test("tests list instruments and lengths readably", () => {
    assert.equal(instrumentLabel("instrument.strings.violin"), "Violin");
    assert.equal(instrumentLabel("instrument.brass.horn.f"), "Horn in F");
    assert.equal(instrumentLabel("instrument.brass.trumpet"), "Trumpet");
    assert.equal(instrumentLabel(""), "");
    assert.equal(formatLength(19200), "19 s");
    assert.equal(formatLength(95000), "1:35");
});

test("state from C++ gets defaults for anything missing", () => {
    const empty = normalizeState(undefined);
    assert.deepEqual(empty.tests, []);
    assert.equal(empty.armed, false);
    assert.equal(empty.lastRun, null);
    const armed = normalizeState({ tests: [{ id: "slurs" }], armed: true, armed_test: "slurs", recorded_notes: 3, discarded_takes: 1 });
    assert.equal(armed.armedTest, "slurs");
    assert.equal(recordingStatus(armed), "3 notes received (latest playthrough; 1 earlier discarded)");
    assert.equal(recordingStatus(normalizeState({ armed: true })), "No notes received yet");
});

test("a run's headline says pass or what failed", () => {
    assert.equal(runHeadline({ pass: true, summary: { expected: 18, passed: 18, received: 18 } }), "Pass: 18 of 18 notes");
    assert.equal(runHeadline({ pass: false, summary: { expected: 18, passed: 15, failed: 2, missing: 1, extra: 0, received: 17 } }),
        "Fail: 2 failed, 1 missing (15 of 18 passed)");
    assert.equal(runHeadline({ pass: false, summary: { expected: 12, passed: 0, received: 0, missing: 12 } }),
        "Fail: 12 missing, no notes received (0 of 12 passed)");
});

test("the pane arms, finishes and copies through C++", () => {
    const pane = readFileSync(new URL("../src/lib/ScoreTests.svelte", import.meta.url), "utf8");
    for (const message of ["requestScoreTests", "armScoreTest", "finishScoreTest", "cancelScoreTest",
        "openScoreTestScore", "revealScoreTestScore", "revealScoreTestRun", "copyScoreTestText"])
        assert.match(pane, new RegExp(`dispatchCpp\\("${message}"`));
    const handlers = readFileSync(new URL("../../ScoreTestUi.cpp", import.meta.url), "utf8");
    for (const message of ["requestScoreTests", "armScoreTest", "finishScoreTest", "cancelScoreTest",
        "openScoreTestScore", "revealScoreTestScore", "revealScoreTestRun", "copyScoreTestText"])
        assert.match(handlers, new RegExp(`registerHandler\\("${message}"`));
    const component = readFileSync(new URL("../../MainComponent.cpp", import.meta.url), "utf8");
    assert.match(component, /id == "score-tests" && type == "scoreTestState"/);
    const native = readFileSync(new URL("../../Main.cpp", import.meta.url), "utf8");
    assert.match(native, /case kScoreTestsMenuItemId: return "score-tests"/);
});
