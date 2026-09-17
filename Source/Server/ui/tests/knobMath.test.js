import assert from "node:assert/strict";
import test from "node:test";
import { gainToPosition, positionToGain, formatGain, parseGain, knobKeyValue } from "../src/lib/knobMath.js";

test("gain taper is monotonic, invertible, and concentrates travel near mix levels", () => {
    let previous = -121;
    for (let i = 0; i <= 1000; i++) {
        const position = i / 1000;
        const db = positionToGain(position);
        assert.ok(db > previous);
        assert.ok(Math.abs(gainToPosition(db) - position) < 1e-10);
        previous = db;
    }
    assert.equal(positionToGain(0), -120);
    assert.equal(positionToGain(1), 12);
    assert.equal(gainToPosition(-12), 0.55);
    assert.equal(gainToPosition(0), 0.8);
    assert.equal(positionToGain(-10), -120);
    assert.equal(positionToGain(10), 12);
});

test("gain text supports exact values and silence without accepting malformed numbers", () => {
    for (const text of ["-∞", "−∞", "-inf", "-Infinity", " −∞ dB "])
        assert.equal(parseGain(text), -120);
    assert.equal(parseGain(" −6.2 dB "), -6.2);
    assert.equal(parseGain("+.5"), 0.5);
    for (const text of ["", " ", "NaN", "Infinity", "12junk", "0x10", "1e99", "+∞"])
        assert.equal(parseGain(text), null);
    assert.equal(formatGain(-120), "−∞");
    assert.equal(formatGain(6), "+6.0");
});

test("keyboard increments have fine mode, useful limits, and ignore unrelated shortcuts", () => {
    const key = (name, value = -12, fine = false) => knobKeyValue(name, value, -120, 12, 0.5, 0.1, fine);
    assert.equal(key("ArrowUp"), -11.5);
    assert.equal(key("ArrowRight", -12, true), -11.9);
    assert.equal(key("ArrowDown"), -12.5);
    assert.equal(key("ArrowLeft", -12, true), -12.1);
    assert.equal(key("PageUp"), -7);
    assert.equal(key("Home"), -120);
    assert.equal(key("End"), 12);
    assert.equal(key("ArrowUp", 12), 12);
    assert.equal(key("ArrowDown", -120), -120);
    assert.equal(key("z"), null);
});
