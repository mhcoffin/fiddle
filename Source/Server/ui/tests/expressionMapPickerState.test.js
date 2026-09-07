import test from "node:test";
import assert from "node:assert/strict";

import {
    getRememberedExpressionMapQuery,
    rememberExpressionMapQuery,
} from "../src/lib/expressionMapPickerState.js";

test("expression-map search is remembered across picker instances", () => {
    rememberExpressionMapQuery("");
    rememberExpressionMapQuery("Syn brass");

    assert.equal(getRememberedExpressionMapQuery(), "Syn brass");
});

test("expression-map search can be cleared", () => {
    rememberExpressionMapQuery("Syn brass");
    rememberExpressionMapQuery("");

    assert.equal(getRememberedExpressionMapQuery(), "");
});
