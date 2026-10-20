// Isolated UI fixtures. Optional real-data audit is read-only and never uses
// the running Fiddle bridge, database, or audio device.
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.FIDDLE_PLAYWRIGHT_MODULE || "playwright");
const dist = fileURLToPath(new URL("../dist/", import.meta.url));
const server = createServer(async (request, response) => {
    const url = new URL(request.url, "http://localhost");
    const file = path.resolve(dist, `.${url.pathname === "/" ? "/index.html" : url.pathname}`);
    if (!file.startsWith(dist)) { response.writeHead(403).end(); return; }
    try {
        response.setHeader("Content-Type", { ".html": "text/html", ".js": "text/javascript", ".css": "text/css" }[path.extname(file)] || "application/octet-stream");
        response.end(await readFile(file));
    } catch { response.writeHead(404).end(); }
});
await new Promise((resolve, reject) => { server.once("error", reject); server.listen(0, "127.0.0.1", resolve); });
let browser;
try {
    browser = await chromium.launch({ channel: process.env.FIDDLE_TEST_BROWSER_CHANNEL || "chrome" });
    const page = await browser.newPage({ viewport: { width: 1200, height: 900 } });
    page.setDefaultTimeout(10000);
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    await page.addInitScript(() => {
        window.messages = [];
        window.__JUCE__ = { backend: { emitEvent: (_, request) => window.messages.push(request.params[0]) } };
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/?mode=technique-catalog`);
    await page.getByRole("main", { name: "Technique Catalog", exact: true }).waitFor();
    console.log("Technique Catalog entry loaded");
    assert.equal(await page.locator(".mixer-container").count(), 0);
    assert.ok(await page.evaluate(() => window.messages.some(m => m.type === "toolWindowReady" && m.payload[0] === "technique-catalog")));
    const report = {
        scannedAt: "2026-09-20T12:00:00Z", files: 3, roots: [], issues: [], unresolvedUsage: [], groups: [],
        sources: [{ kind: "installed", path: "/fixture/VSL.doricolib" }, { kind: "installed", path: "/fixture/ArtConductor.doricolib" }, { kind: "reference", path: "/fixture/unowned-patches.doricolib" }],
        maps: [
            { id: "map.vsl", name: "VSL Violin", source: 0, usage: [{ scope: "project", label: "Violin 1", exactSource: true }], groups: [] },
            { id: "map.ac", name: "Art Conductor Violin", source: 1, usage: [{ scope: "library", label: "Violin patch" }], groups: [] },
            { id: "map.organ", name: "Organ", source: 2, usage: [], groups: [] },
        ],
        techniques: Array.from({ length: 85 }, (_, i) => ({
            id: `pt.example.${i}`, names: [i ? `Technique ${i}` : "Lyrical"], maps: [0, 1], declared: !!(i % 2), definitionState: "local", variants: false,
            definitions: [{ sources: [0, 1], name: "Lyrical", articulationType: "kAttribute", groups: [] }],
            appearances: [{ sources: [1], name: "Lyrical line", id: "pta.lyrical", text: "lyrical", type: "kText", alwaysHasDuration: "true", durationLine: "line.hook" }],
        })),
    };
    report.techniques.push({ id: "pt.organ", names: ["Organ stop"], maps: [2], declared: false, definitionState: "reference", variants: false, definitions: [], appearances: [] });
    report.reachability = { decoderAvailable: true, chairs: [
        { id: "violin", name: "Violin 1", profile: "builtin-universal", required: ["pt.staccato", "pt.user.lyrical"], layers: [
            { id: "vsl", name: "VSL layer", assigned: true, mapId: "map.vsl", mapName: "VSL Violin", rows: [
                { index: 0, name: "Staccato", techniques: ["pt.staccato"], status: "witness", reason: "A state selects this entry.", witness: { cc: [{ cc: 102, value: 5 }], length: 0, techniques: ["pt.staccato"], base: "Staccato" } },
                { index: 1, name: "Lyrical", techniques: ["pt.user.lyrical"], missing: ["pt.user.lyrical"], status: "missing", reason: "Identity not produced." },
            ] },
            { id: "ac", name: "Art Conductor layer", assigned: true, mapId: "map.ac", mapName: "AC Violin", rows: [{ index: 0, name: "Lyrical", techniques: ["pt.user.lyrical"], status: "missing", reason: "Identity not produced." }] },
        ] },
        { id: "flute", name: "Flute 1", required: [], layers: [{ id: "empty", name: "Unmapped flute", assigned: false }] },
    ] };
    report.repairPlan = {
        planId: "fixture-plan-1", preserveIdentities: true, metadataComplete: true, canonicalPreview: true,
        selectedAdditions: [], freeControllers: [114, 115], allocated: 0, warnings: [],
        before: { total: 3, witness: 1 }, after: { total: 3, witness: 2 }, gained: 1, regressed: 0,
        identityChanges: [{ cc: 108, value: 1, before: ["pt.sultasto"], after: ["pt.sulTasto"] }],
        candidates: [
            { id: "pt.user.long", eligible: true, selected: false, appearanceCount: 1, reason: "Propose independent on/off CC.", affected: [{ chair: "Violin 1", layer: "BBC", map: "BBC", entry: "Long", index: 0 }] },
            { id: "pt.vibrato", eligible: false, selected: false, appearanceCount: 0, reason: "Reset/default ambiguity needs review.", affected: [] },
        ], changes: [], remaining: [],
    };
    report.chairCoverage = {
        modelRevision: "chair-sizing-v1", limits: { statesPerChair: 4096, resolverCalls: 100000, milliseconds: 5000 },
        chairs: [{ id: "violin", name: "Violin 1", estimatedStates: 96, completedStates: 96, resolverCalls: 1440,
            enumerationStatus: "complete", stopReason: "", issueCount: 1,
            dimensions: [{ name: "Connection", kind: "enum", stateCount: 2, values: ["pt.legato"] }],
            vocabulary: [{ id: "pt.legato", builtin: true, reasons: [{ layerName: "VSL", mapName: "VSL Violin", entry: 0, entryName: "Legato" }] }],
            layers: [{ id: "vsl", name: "VSL", assigned: true, mapName: "VSL Violin", outcomes: { covered: 10, partial: 80, fallback: 12 } }],
            issues: [{ code: "contract-review", message: "Cancellation/default rules still need review.", techniques: [], entry: -1 }],
            witnesses: [{ techniques: [], interpretations: [{ layerName: "VSL", length: 0, outcome: "fallback", base: { name: "Natural" }, addons: [], unmatched: [], introduced: ["pt.natural"] }] },
                { techniques: ["pt.legato", "pt.muted"], interpretations: [{ layerName: "VSL", length: 4, outcome: "partial", base: { name: "Legato" }, addons: [], unmatched: ["pt.muted"], introduced: [] }] }],
        }, { id: "flute", name: "Flute 1", estimatedStates: 8192, completedStates: 4096, resolverCalls: 20480,
            enumerationStatus: "partial", stopReason: "state-limit", issueCount: 0, dimensions: [], vocabulary: [], layers: [], issues: [], witnesses: [] }],
    };
    const publish = async state => page.evaluate(state => window.__dispatchFromCpp({ type: "setTechniqueCatalogState", data: state }), state);
    await publish({ running: false, files: 3, report, includeReference: false });
    console.log("Fixture report published");
    await page.getByRole("heading", { name: "What can your current chairs request?" }).waitFor();
    assert.equal(await page.locator("tbody tr").count(), 3);
    assert.match(await page.getByLabel("Reachability details", { exact: true }).innerText(), /CC102 = 5/);
    await page.getByLabel("Audit result", { exact: true }).selectOption("attention");
    assert.equal(await page.locator("tbody tr").count(), 2);
    await page.getByLabel("Search audit entries", { exact: true }).fill("Art Conductor");
    assert.equal(await page.locator("tbody tr").count(), 1);
    assert.match(await page.getByLabel("Reachability details", { exact: true }).innerText(), /AC Violin/);
    await page.getByLabel("Search audit entries", { exact: true }).fill("");
    await page.getByLabel("Audit chair", { exact: true }).selectOption("flute");
    assert.match(await page.locator(".stats").innerText(), /1 layer\(s\) without/);
    await page.getByLabel("Audit chair", { exact: true }).selectOption("violin");
    await page.getByLabel("Audit result", { exact: true }).selectOption("all");
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "bridge-reachability-fixture.png") });
    await page.setViewportSize({ width: 480, height: 420 });
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await page.setViewportSize({ width: 1200, height: 900 });
    await page.getByRole("button", { name: "Chair coverage", exact: true }).click();
    const coverage = page.getByRole("region", { name: "Chair coverage", exact: true });
    assert.equal(await page.getByLabel("Coverage chair", { exact: true }).inputValue(), "violin");
    assert.match(await coverage.innerText(), /96 candidate requests/);
    assert.match(await coverage.innerText(), /Enumeration complete for this sizing domain/);
    assert.match(await coverage.innerText(), /not an installation-ready map/);
    await page.getByLabel("Coverage example", { exact: true }).selectOption("1");
    assert.match(await page.getByRole("table", { name: "Coverage interpretations" }).innerText(), /pt.muted/);
    await page.getByText("Dimensions and vocabulary provenance", { exact: true }).click();
    assert.match(await coverage.innerText(), /Connection/);
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "chair-coverage-fixture.png") });
    await page.getByLabel("Coverage chair", { exact: true }).selectOption("flute");
    assert.match(await coverage.innerText(), /Partial \/ unknown — state-limit/);
    assert.equal(await page.getByLabel("Coverage example", { exact: true }).count(), 0);
    await page.getByLabel("Coverage chair", { exact: true }).selectOption("violin");
    assert.equal(await page.getByLabel("Coverage example", { exact: true }).inputValue(), "0");
    await page.setViewportSize({ width: 480, height: 420 });
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await page.setViewportSize({ width: 1200, height: 900 });
    await page.getByRole("button", { name: "Repair plan", exact: true }).click();
    assert.match(await page.getByLabel("Repair coverage comparison").innerText(), /2 \/ 3/);
    assert.ok(await page.getByLabel("Include pt.vibrato", { exact: true }).isDisabled());
    await page.getByLabel("Include pt.user.long", { exact: true }).check();
    assert.ok(await page.getByRole("button", { name: "Export plan (JSON)…", exact: true }).isDisabled());
    await page.getByRole("button", { name: "Preview selected repairs", exact: true }).click();
    assert.ok(await page.evaluate(() => window.messages.some(m => m.type === "previewBridgeRepairs" && m.payload[0] === true && m.payload[1][0] === "pt.user.long")));
    await publish({ running: true, previewing: true, files: 3, report, includeReference: false });
    assert.match(await page.getByRole("status").innerText(), /existing snapshot/);
    assert.ok(await page.getByRole("button", { name: "Preview selected repairs", exact: true }).isDisabled());
    report.repairPlan = { ...report.repairPlan, planId: "fixture-plan-2", selectedAdditions: ["pt.user.long"], allocated: 1,
        candidates: report.repairPlan.candidates.map(c => c.id === "pt.user.long" ? { ...c, selected: true, cc: 114 } : c),
        after: { total: 3, witness: 3 }, gained: 2 };
    await publish({ running: false, files: 3, report, includeReference: false });
    assert.match(await page.getByLabel("Bridge repair plan", { exact: true }).innerText(), /Proposed CC114/);
    await page.getByRole("button", { name: "Export plan (JSON)…", exact: true }).click();
    assert.ok(await page.evaluate(() => window.messages.some(m => m.type === "exportBridgeRepairPlan" && m.payload[0] === "fixture-plan-2")));
    await page.getByLabel("Preserve exact technique IDs", { exact: true }).uncheck();
    assert.ok(await page.getByRole("button", { name: "Export plan (JSON)…", exact: true }).isDisabled());
    await page.getByLabel("Preserve exact technique IDs", { exact: true }).check();
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "bridge-repair-plan-fixture.png") });
    await page.setViewportSize({ width: 480, height: 420 });
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await page.setViewportSize({ width: 1200, height: 900 });
    await page.getByRole("button", { name: "Vocabulary catalog", exact: true }).click();
    const scope = page.getByLabel("Scope", { exact: true });
    assert.equal(await scope.inputValue(), "project");
    assert.equal(await page.locator("tbody tr").count(), 80);
    assert.match(await page.locator("footer").innerText(), /85 techniques/);
    await page.getByRole("button", { name: "Next", exact: true }).click();
    assert.equal(await page.locator("tbody tr").count(), 5);
    await page.getByRole("button", { name: "Previous", exact: true }).click();
    await page.getByLabel("Search techniques", { exact: true }).fill("lyrical");
    assert.equal(await page.locator("tbody tr").count(), 80); // markings are searchable, not just display names
    await page.getByLabel("Search techniques", { exact: true }).fill("pt.example.0");
    assert.equal(await page.locator("tbody tr").count(), 1);
    assert.match(await page.getByLabel("Technique details", { exact: true }).innerText(), /Lyrical line/);
    await page.getByLabel("Search techniques", { exact: true }).fill("");
    await page.getByLabel("Coverage", { exact: true }).selectOption("gap");
    assert.match(await page.locator("footer").innerText(), /43 techniques/);
    await page.getByLabel("Coverage", { exact: true }).selectOption("all");
    await scope.selectOption("installed");
    assert.equal(await page.getByLabel("Map", { exact: true }).locator("option").count(), 3);
    await page.getByLabel("Map", { exact: true }).selectOption("1");
    assert.match(await page.locator("aside").innerText(), /Art Conductor Violin/);
    assert.doesNotMatch(await page.locator("aside .map-detail").innerText(), /VSL Violin/);
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "technique-catalog-fixture.png") });
    await scope.selectOption("reference");
    assert.match(await page.locator("tbody").innerText(), /Organ stop/);
    assert.doesNotMatch(await page.locator("tbody").innerText(), /Lyrical/);
    await page.getByRole("checkbox").check();
    await page.getByRole("button", { name: "Refresh catalog", exact: true }).click();
    assert.ok(await page.evaluate(() => window.messages.some(m => m.type === "scanTechniqueCatalog" && m.payload[0] === true)));
    await publish({ running: true, files: 123, includeReference: true });
    assert.match(await page.getByRole("status").innerText(), /123/);
    assert.ok(await page.getByRole("button", { name: "Refresh catalog", exact: true }).isDisabled());
    await page.getByRole("button", { name: "Cancel scan", exact: true }).click();
    assert.ok(await page.evaluate(() => window.messages.some(m => m.type === "cancelTechniqueCatalogScan")));
    await publish({ running: false, files: 3, report: { ...report, cancelled: true }, includeReference: true });
    assert.match(await page.getByRole("status").innerText(), /partial/);
    await page.evaluate(() => window.__dispatchFromCpp({ type: "setToolWindowVisible", data: false }));
    const polls = await page.evaluate(() => window.messages.filter(m => m.type === "requestTechniqueCatalogState").length);
    await page.waitForTimeout(1000);
    assert.equal(await page.evaluate(() => window.messages.filter(m => m.type === "requestTechniqueCatalogState").length), polls);
    await page.evaluate(() => window.__dispatchFromCpp({ type: "setToolWindowVisible", data: true }));
    await page.setViewportSize({ width: 480, height: 420 });
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await page.getByRole("button", { name: "Close", exact: true }).click();
    assert.ok(await page.evaluate(() => window.messages.some(m => m.type === "hideToolWindow" && m.payload[0] === "technique-catalog")));

    // Explicit opt-in real data: FIDDLE_TECHNIQUE_SCAN_TOOL is the native test
    // executable and FIDDLE_TECHNIQUE_SCAN_ARGS is a JSON array of source args.
    if (process.env.FIDDLE_TECHNIQUE_SCAN_TOOL) {
        const args = JSON.parse(process.env.FIDDLE_TECHNIQUE_SCAN_ARGS || "[]");
        const real = JSON.parse(execFileSync(process.env.FIDDLE_TECHNIQUE_SCAN_TOOL, ["--json", ...args], { encoding: "utf8", maxBuffer: 128 * 1024 * 1024 }));
        await page.setViewportSize({ width: 1200, height: 900 });
        await publish({ running: false, files: real.files, report: real, includeReference: args.includes("--reference") });
        await scope.selectOption("installed");
        await page.getByLabel("Coverage", { exact: true }).selectOption("gap");
        assert.ok(await page.locator("tbody tr").count() <= 80);
        assert.ok(real.techniques.length > 0);
        if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
            await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "technique-catalog-real.png") });
        console.log(`Real-data view: ${real.files} files, ${real.maps.length} maps, ${real.techniques.length} playback identities, ${real.issues.length} scan issues`);
    }
    // Bridge authoring is accessible without a catalog scan and uses separate
    // write commands. All payloads here are fixtures, never a live server.
    await page.setViewportSize({ width: 1200, height: 900 });
    await page.getByRole("button", { name: "Bridge maps", exact: true }).click();
    await page.getByRole("region", { name: "Bridge maps", exact: true }).waitFor();
    assert.ok(await page.evaluate(() => window.messages.some(m => m.type === "requestBridgeMaps")));
    const bridgeState = {
        documents: [{ hash: "strings-hash", name: "Fiddle Bridge - Orchestral Strings r4 (host test)", revision: 4, schema: 2, entityId: "xmap.fixture.strings", sourceXml: "<fixture />", low: 21, high: 114, dimensions: 33 }],
        sections: [{ key: "strings", label: "Orchestral strings", hash: "strings-hash" }, { key: "brass", label: "Orchestral brass", hash: "" }],
        assignments: [{ port: 0, channel: 2, hash: "strings-hash", verified: false, beacon: "absent", receivedBeacon: [-1, -1], expectedBeacon: [81, 22], lastWithheld: "", withheldNotes: 0, invalidCommands: 0, ignoredOffs: 0, debouncedOffs: 0 }],
        error: "", busy: false,
    };
    await page.evaluate(data => window.__dispatchFromCpp({ type: "setBridgeMapState", data }), bridgeState);
    let bridgeText = await page.locator(".bridges").innerText();
    assert.match(bridgeText, /Orchestral Strings r4 \(host test\) · revision 4 · wire schema 2/);
    assert.match(bridgeText, /Not set yet · install the playback template from Chairs/);
    assert.match(bridgeText, /Port 1, channel 2\s+Orchestral strings\s+Waiting for the revision beacon/);
    // Read-only: no manual assignment, template or XML controls remain.
    for (const name of ["Assign revision", "Woodwinds", "Verify assigned map…", "Publish new revision", "Import map or bundle…"])
        assert.equal(await page.getByRole("button", { name, exact: true }).count(), 0, name);
    await page.getByRole("button", { name: "Export Dorico map…", exact: true }).click();
    assert.ok(await page.evaluate(() => window.messages.some(m => m.type === "exportBridgeMap" && m.payload[0] === "strings-hash")));
    const live = { ...bridgeState.assignments[0], beacon: "mismatched", receivedBeacon: [3, 4],
        lastWithheld: "Dorico is playing a different bridge revision", withheldNotes: 2, ignoredOffs: 1, debouncedOffs: 3 };
    await page.evaluate(data => window.__dispatchFromCpp({ type: "setBridgeMapState", data }), { ...bridgeState, assignments: [live] });
    bridgeText = await page.locator(".bridges").innerText();
    assert.match(bridgeText, /different revision \(beacon 3\/4, expected 81\/22\)/);
    assert.match(bridgeText, /2 notes withheld · 1 stale Offs ignored · 3 early Offs suppressed/);
    await page.evaluate(data => window.__dispatchFromCpp({ type: "setBridgeMapState", data }), { ...bridgeState, assignments: [{ ...live, beacon: "matched", lastWithheld: "", withheldNotes: 0 }] });
    assert.match(await page.locator(".bridges").innerText(), /Beacon matches/);
    await page.evaluate(data => window.__dispatchFromCpp({ type: "setBridgeMapState", data }), { ...bridgeState, error: "Built-in bridge map is missing: x" });
    assert.match(await page.getByRole("alert").innerText(), /Built-in bridge map is missing/);
    await page.getByRole("button", { name: "Refresh live status", exact: true }).click();
    assert.ok(await page.evaluate(() => window.messages.filter(m => m.type === "requestBridgeMaps").length >= 2));
    await page.setViewportSize({ width: 480, height: 650 });
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "bridge-maps-fixture.png") });
    assert.deepEqual(errors, []);
    console.log("PASS: technique catalog and read-only bridge sections, live status, export, and narrow layout");
} finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
}
