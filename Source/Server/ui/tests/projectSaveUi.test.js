import assert from "node:assert/strict";
import test from "node:test";
import { historicalSaveNotice, projectSaveButton } from "../src/lib/projectSaveUi.js";

test("save is available for edited head and historical versions", () => {
    for (const historical of [false, true]) {
        assert.equal(projectSaveButton(false, historical).disabled, true);
        assert.equal(projectSaveButton(true, historical).disabled, false);
    }
});

test("historical save affordances explain branching rather than forbid saving", () => {
    assert.match(projectSaveButton(true, true).title, /new branch/);
    assert.match(historicalSaveNotice, /saving changes creates a new branch/);
    assert.doesNotMatch(projectSaveButton(true, false).title, /branch/);
});
