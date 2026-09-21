import assert from "node:assert/strict";
import test from "node:test";
import { auditRows, auditCounts } from "../src/lib/bridgeReachability.js";
const audit = { chairs: [
    { id: "chair1", name: "Violins", layers: [
        { id: "a", name: "VSL", mapId: "m1", mapName: "Violin", rows: [{ index: 0, name: "Short", techniques: ["pt.staccato"], status: "witness" }] },
        { id: "b", name: "BBC", mapId: "m2", mapName: "Violin", rows: [{ index: 0, name: "Short", techniques: ["pt.user.short"], status: "missing" }] },
    ] },
    { id: "chair2", name: "Flutes", layers: [{ id: "c", name: "Unmapped", assigned: false }] },
] };
test("chair audit keeps every layer and alternative map identity separate", () => {
    const rows = auditRows(audit);
    assert.equal(rows.length, 2);
    assert.notEqual(rows[0].key, rows[1].key);
    assert.equal(auditRows(audit, { chairId: "chair2" }).length, 0);
    assert.equal(auditRows(audit, { chairId: "chair1" }).length, 2);
    assert.equal(auditRows(null).length, 0);
});
test("audit filters find actionable entries and their exact source layer", () => {
    assert.equal(auditRows(audit, { status: "attention" })[0].layerId, "b");
    assert.equal(auditRows(audit, { status: "witness" })[0].mapId, "m1");
    assert.equal(auditRows(audit, { query: "PT.USER.SHORT" })[0].layerId, "b");
    assert.equal(auditRows(audit, { query: "VSL" }).length, 1);
    assert.equal(auditCounts(auditRows(audit)).missing, 1);
});
