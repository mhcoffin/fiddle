import assert from "node:assert/strict";
import test from "node:test";
import { historyShortcut, installHistoryGestures } from "../src/lib/historyGestures.js";

test("Undo and Redo are case-independent and preserve native text undo", () => {
    assert.equal(historyShortcut({ metaKey: true, key: "Z", shiftKey: true }), "redo");
    assert.equal(historyShortcut({ ctrlKey: true, key: "y" }), "redo");
    assert.equal(historyShortcut({ metaKey: true, key: "z" }), "undo");
    for (const target of [{ tagName: "INPUT", type: "text" }, { tagName: "INPUT", type: "number" },
        { tagName: "TEXTAREA" }, { isContentEditable: true }])
        assert.equal(historyShortcut({ metaKey: true, key: "z", target }), null);
    assert.equal(historyShortcut({ metaKey: true, key: "z", defaultPrevented: true }), null);
});

test("range drags and key repeats have explicit boundaries; typed values commit once", async () => {
    const handlers = new Map(), sent = [];
    const root = { addEventListener: (n, f) => handlers.set(n, f), removeEventListener: n => handlers.delete(n) };
    const dispose = installHistoryGestures(root, n => sent.push(n));
    const target = { tagName: "INPUT", type: "range" };
    handlers.get("pointerdown")({ target });
    handlers.get("blur")({ target: { tagName: "INPUT", type: "text" } });
    handlers.get("keydown")({ target, key: "ArrowUp" });
    handlers.get("keydown")({ target, key: "ArrowUp", repeat: true });
    assert.deepEqual(sent, ["beginHistoryGesture"]);
    handlers.get("pointerup")({});
    handlers.get("change")({ target: { tagName: "INPUT", type: "number" } });
    await Promise.resolve();
    assert.deepEqual(sent, ["beginHistoryGesture", "endHistoryGesture", "beginHistoryGesture", "endHistoryGesture"]);
    handlers.get("pointerdown")({ target });
    handlers.get("blur")({ target: root });
    assert.deepEqual(sent.slice(-2), ["beginHistoryGesture", "endHistoryGesture"]);
    dispose();
    assert.equal(handlers.size, 0);
});
