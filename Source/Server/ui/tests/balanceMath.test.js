import assert from "node:assert/strict";
import test from "node:test";
import { formatBalance, parseBalance } from "../src/lib/balanceMath.js";

test("balance text has an unambiguous center and direction", () => {
    for (const [value, text] of [[0, "C"], [-100, "L100"], [100, "R100"], [-25, "L25"], [50, "R50"]]) {
        assert.equal(formatBalance(value), text);
        assert.equal(parseBalance(text), value);
    }
    for (const [text, value] of [[" center ", 0], ["-25", -25], ["+50", 50], ["25 l", -25], ["r 25", 25]])
        assert.equal(parseBalance(text), value);
    for (const text of ["", " ", "NaN", "Infinity", "L101", "-101", "25wat", "L-20", "0x10"])
        assert.equal(parseBalance(text), null);
});
