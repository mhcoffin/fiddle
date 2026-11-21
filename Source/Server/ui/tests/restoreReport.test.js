import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { restoreBannerLook, restoreProblems, showRestoreReport } from "../src/lib/restoreReport.js";

test("layers that can't play as saved are listed by kind", () => {
    const problems = restoreProblems([
        { layerName: "Concert Grand", library: "Synchron Pianos", pluginStatus: "missing" },
        { layerName: "Violins", pluginStatus: "loaded", luaPlugins: [{ name: "humanize", loaded: false }] },
        { layerName: "Horns", pluginStatus: "failed" },
        { layerName: "Flute", pluginStatus: "loading" },
    ]);
    assert.equal(problems.length, 3);
    assert.match(problems[0], /not installed, so this layer is silent: Concert Grand \(Synchron Pianos\)/);
    assert.match(problems[1], /failed to load: Horns/);
    assert.match(problems[2], /humanize on Violins/);
    assert.deepEqual(restoreProblems(undefined), []);
});

test("long lists are shortened", () => {
    const strips = Array.from({ length: 7 }, (_, i) => ({ layerName: `L${i}`, pluginStatus: "missing" }));
    assert.match(restoreProblems(strips)[0], /L0, L1, L2, L3 and 3 more/);
});

test("a plain successful restore shows only when something can't play", () => {
    assert.equal(showRestoreReport({ kind: "exact" }, []), false);
    assert.equal(showRestoreReport({ kind: "exact" }, ["x"]), true);
    assert.equal(showRestoreReport({ kind: "exact", savedEdits: true }, []), true);
    assert.equal(showRestoreReport({ kind: "exact", changes: ["Fiddle has been updated"] }, []), true);
    assert.equal(showRestoreReport({ kind: "exact", changes: [] }, []), false);
    assert.equal(showRestoreReport({ kind: "notFound" }, []), true);
    assert.equal(showRestoreReport({ kind: "boundToCurrent" }, []), true);
    assert.equal(showRestoreReport(null, ["x"]), false);
});

test("the main window shows the restore report with its actions", () => {
    const app = readFileSync(new URL("../src/App.svelte", import.meta.url), "utf8");
    assert.match(app, /<RestoreReport \/>/);
    const banner = readFileSync(new URL("../src/lib/RestoreReport.svelte", import.meta.url), "utf8");
    assert.match(banner, /report\.options/);
    assert.match(banner, /Since this project was saved/);
    for (const message of ["dismissRestoreReport", "useCurrentSetupForProject", "retryProjectRestore"])
        assert.match(banner, new RegExp(`dispatchCpp\\("${message}"\\)`));
});

test("an exact restore with silent layers warns like one with changes", () => {
    const plain = { kind: "exact", severity: "info", title: "Dorico project connected" };
    assert.deepEqual(restoreBannerLook(plain, []), { warning: false, title: "Dorico project connected" });
    assert.deepEqual(restoreBannerLook(plain, ["Plug-in not installed"]),
        { warning: true, title: "This project may sound different than when it was saved" });
    const missing = { kind: "notFound", severity: "warning", title: "This project's Fiddle setup isn't on this Mac" };
    assert.deepEqual(restoreBannerLook(missing, ["x"]), { warning: true, title: missing.title });
});
