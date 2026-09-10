// Optional rendered-layout regression. Build the UI first, then run with Node.
// Requires Playwright and Chrome (or set FIDDLE_TEST_BROWSER_CHANNEL).
// FIDDLE_PLAYWRIGHT_MODULE may point to an externally installed Playwright.
// Serves only the built UI on an ephemeral loopback port; no live Fiddle state.
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { createRequire } from "node:module";
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
        const content = await readFile(file);
        response.setHeader("Content-Type", {
            ".html": "text/html", ".js": "text/javascript", ".css": "text/css",
        }[path.extname(file)] || "application/octet-stream");
        response.end(content);
    } catch { response.writeHead(404).end(); }
});
await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
});

let browser;
try {
    browser = await chromium.launch({ channel: process.env.FIDDLE_TEST_BROWSER_CHANNEL || "chrome" });
    const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
    page.setDefaultTimeout(10000);
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.waitForSelector(".mixer-container");
    await page.evaluate(() => {
        const chairs = Array.from({ length: 10 }, (_, index) => ({
            id: `chair-${index}`, name: index === 1 ? "A very long orchestral chair name" : `Flute ${index + 1}`,
            family: "wind", role: "solo", port: 0, channel: index,
            ordinal: index + 1, displayOrder: index, flatIndex: index,
        }));
        const strips = chairs.flatMap((chair, index) => index === 9 ? [] :
            Array.from({ length: index === 8 ? 2 : 1 }, (_, layer) => ({
                id: `${chair.id}-${layer}`, chairId: chair.id, inputPort: 0,
                inputChannel: index, library: "SY WW", layerName: chair.name, family: "wind",
                isSolo: true, active: true, gainDb: 0, muted: false, soloed: false,
                pluginUid: index === 2 ? 404 : (index % 2 === 0 ? 101 : 0),
                hasPlugin: index !== 2 && index % 2 === 0,
            })));
        window.fixtureStrips = strips;
        window.fixtureMessages = [];
        window.__JUCE__ = { backend: { emitEvent: (_event, request) => {
            window.fixtureMessages.push(request.params[0]);
        } } };
        window.__dispatchFromCpp({ type: "setPluginList", data: [
            { uid: 101, name: "A very long sample player name", valid: true },
            { uid: 102, name: "Another player", valid: true },
            { uid: 103, name: "Invalid player", valid: false },
        ] });
        window.__dispatchFromCpp({ type: "setChairState", data: chairs });
        window.__dispatchFromCpp({ type: "setMixerState", data: strips });
    });
    await page.waitForSelector(".channel-strip");
    const sizes = [];
    for (const size of ["compact", "comfortable", "large"]) {
        await page.locator("#strip-size-select").selectOption(size);
        await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
        const measured = await page.evaluate(() => ({
            scrollWidth: document.querySelector(".console").scrollWidth,
            groups: [...document.querySelectorAll(".inst-group")].map(group => {
                const rect = group.getBoundingClientRect();
                const header = group.querySelector(".bridge-header");
                const children = [...group.querySelectorAll(".channel-strip, .master-strip, .empty-chair")];
                return {
                    width: rect.width,
                    contentWidth: children.reduce((sum, child) => sum + child.getBoundingClientRect().width, 0),
                    headerHeight: header.getBoundingClientRect().height,
                    headerOverflow: header.scrollWidth - header.clientWidth,
                    faderTops: [...group.querySelectorAll(".fader-track")].map(track => track.getBoundingClientRect().top),
                    // Master strips have their own existing bottom-control
                    // geometry; compare the layer strips' complete spans.
                    faderBottoms: [...group.querySelectorAll(".channel-strip .fader-track")].map(track => track.getBoundingClientRect().bottom),
                    buttonFits: (() => {
                        const button = group.querySelector(".bridge-add-layer").getBoundingClientRect();
                        return button.left >= rect.left && button.right <= rect.right;
                    })(),
                };
            }),
        }));
        console.log(`${size}: single chair ${measured.groups[0].width}px; multi-layer chair ${measured.groups[8].width}px; total scroll width ${measured.scrollWidth}px`);
        sizes.push(measured);
        for (const group of measured.groups) {
            assert.ok(Math.abs(group.width - group.contentWidth - 2) <= 1,
                `${size}: chair width must follow strips, not header content`);
            assert.ok(group.headerOverflow <= 1, `${size}: header must not overflow`);
            assert.ok(group.buttonFits, `${size}: Add Layer button must fit`);
        }
        const heights = measured.groups.map(group => group.headerHeight);
        assert.ok(Math.max(...heights) - Math.min(...heights) <= 1, "headers must align");
        const tops = measured.groups.flatMap(group => group.faderTops);
        assert.ok(Math.max(...tops) - Math.min(...tops) <= 1, "faders must align");
        const bottoms = measured.groups.flatMap(group => group.faderBottoms);
        if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR) {
            await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, `${size}.png`) });
        }
        assert.ok(Math.max(...bottoms) - Math.min(...bottoms) <= 1, `fader spans must match: ${bottoms.join(", ")}`);
        const firstChair = page.locator(".inst-group").first();
        const firstStrip = firstChair.locator(".channel-strip");
        assert.equal(await firstChair.getByText("Flute 1", { exact: true }).count(), 1,
            "chair name must not be repeated as a patch label");
        assert.equal(await firstStrip.locator(".ch-strip-identity").innerText(), "SY WW");
        assert.equal(await firstStrip.locator(".ch-instrument-summary select").count(), 0);
        await firstStrip.getByRole("button", { name: "Edit VSTi", exact: true }).click();
        assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
            { type: "toggleStripEditor", payload: ["chair-0-0"] });
        await page.evaluate(() => window.__dispatchFromCpp({ type: "setInstrumentEditorState",
            data: { stripId: "chair-0-0", editorOpen: true } }));
        const hideButton = firstStrip.getByRole("button", { name: "Hide VSTi", exact: true });
        await hideButton.click();
        assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
            { type: "toggleStripEditor", payload: ["chair-0-0"] });
        await page.evaluate(() => window.__dispatchFromCpp({ type: "setInstrumentEditorState",
            data: { stripId: "chair-0-0", editorOpen: false } }));
        await firstStrip.getByRole("button", { name: "Edit VSTi", exact: true }).waitFor();
        // Native-window close notifications use the same authoritative state.
        await page.evaluate(() => {
            for (const editorOpen of [true, false])
                window.__dispatchFromCpp({ type: "setInstrumentEditorState",
                    data: { stripId: "chair-0-0", editorOpen } });
        });
        await firstStrip.getByRole("button", { name: "Edit VSTi", exact: true }).waitFor();
        assert.equal(await page.locator(".save-btn").isDisabled(), true,
            "editor visibility must not enable Save");
        const controlHeights = await firstStrip.evaluate(strip => ({
            vst: strip.querySelector(".ch-instrument-summary button").getBoundingClientRect().height,
            fx: strip.querySelector(".ch-audio-fx").getBoundingClientRect().height,
        }));
        assert.equal(controlHeights.vst, 32);
        assert.equal(controlHeights.fx, controlHeights.vst, "VSTi and Audio FX buttons must have equal height");

        const secondStrip = page.locator(".inst-group").nth(1).locator(".channel-strip");
        const selector = secondStrip.getByRole("combobox", { name: "Choose VSTi" });
        assert.equal(await selector.inputValue(), "0", "empty player must display a prompt, not a blank selection");
        assert.match(await selector.locator("option:checked").innerText(), /VSTi/);
        assert.equal(await selector.locator('option[value="103"]').count(), 0);
        await selector.selectOption("102");
        assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
            { type: "setStripPlugin", payload: ["chair-1-0", 102] });
        // Simulate the normal server acknowledgement, then verify selector -> editor.
        await page.evaluate(() => window.__dispatchFromCpp({ type: "setMixerState",
            data: window.fixtureStrips.map(strip => strip.id === "chair-1-0"
                ? { ...strip, hasPlugin: true, pluginUid: 102 } : strip) }));
        await secondStrip.getByRole("button", { name: "Edit VSTi", exact: true }).waitFor();
        const missing = page.locator(".inst-group").nth(2).getByRole("combobox", { name: "Choose VSTi" });
        assert.equal(await missing.inputValue(), "404", "retain unavailable instrument identity");
        await page.evaluate(() => window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips }));

        const controlsFit = await page.locator(".ch-instrument-summary").evaluateAll(rows => rows.every(row => {
            const box = row.getBoundingClientRect();
            const control = row.firstElementChild.getBoundingClientRect();
            return control.left >= box.left && control.right <= box.right + 1
                && control.top >= box.top && control.bottom <= box.bottom + 1;
        }));
        assert.ok(controlsFit, `${size}: VSTi buttons/selectors must fit without overlap`);
        await page.getByRole("button", { name: "Add a layer to Flute 1", exact: true }).click();
        const picker = page.getByRole("dialog", { name: "Add layer to Flute 1", exact: true });
        await picker.waitFor({ state: "visible" });
        await picker.getByRole("button", { name: "Cancel", exact: true }).click();
        await picker.waitFor({ state: "hidden" });
    }
    assert.ok(sizes[0].scrollWidth < sizes[1].scrollWidth);
    assert.ok(sizes[1].scrollWidth < sizes[2].scrollWidth);
    // Audio diagnostics use isolated fixtures, never the live server/device.
    await page.evaluate(() => window.__dispatchFromCpp({ type: "setAudioDiagnostics", data: {
        running: true, ageMs: 0, load: 70, peakLoad: 88, sampleRate: 44100,
        blockSize: 512, buildConfiguration: "Release", returnFresh: true, returnSampleRate: 44100,
        device: { name: "Fixture audio interface", type: "Test audio", sampleRate: 44100, bufferSize: 512 },
        plugins: Array.from({ length: 30 }, (_, i) => ({
            id: `slot-${i}`, owner: `Violin ${i + 1} / A long library name`, kind: "Instrument",
            plugin: "Vienna Synchron Player", averageMs: i / 10, peakMs: 3, maxMs: 5,
            ageMs: 100, blockSize: 512, sampleRate: 44100,
        })),
    } }));
    await page.getByRole("button", { name: "Audio CPU 70%", exact: true }).click();
    const performance = page.getByRole("dialog", { name: "Audio performance", exact: true });
    await performance.waitFor({ state: "visible" });
    assert.equal(await performance.locator("tbody tr").count(), 30);
    assert.match(await performance.locator("tbody tr").first().innerText(), /Violin 30/);
    for (const name of ["Close", "Mark crackle", "Copy report"]) {
        const rect = await performance.getByRole("button", { name, exact: true }).boundingBox();
        assert.ok(rect && rect.y >= 0 && rect.y + rect.height <= 900, `${name} must stay visible with many plugins`);
    }
    await performance.getByRole("button", { name: "Mark crackle", exact: true }).click();
    await performance.getByRole("button", { name: "Copy report", exact: true }).click();
    const copied = await page.evaluate(() => window.fixtureMessages.findLast(x => x.type === "copyAudioDiagnosticsReport"));
    assert.ok(copied, "copy button must dispatch a diagnostic report");
    const report = JSON.parse(copied.payload[0]);
    assert.equal(report.marks.length, 1);
    assert.equal(report.marks[0].snapshot.plugins.length, 30);
    assert.equal(report.samples[0].device.bufferSize, 512);
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "audio-performance.png") });
    await performance.locator("tbody tr").first().scrollIntoViewIfNeeded();
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "audio-plugin-timings.png") });
    await performance.getByRole("button", { name: "Audio Settings…", exact: true }).click();
    await performance.waitFor({ state: "hidden" });
    assert.ok(await page.evaluate(() => window.fixtureMessages.some(x => x.type === "showAudioSettings")));
    assert.deepEqual(errors, [], "no browser runtime errors");
    console.log("PASS: chair density/controls and audio diagnostics table, fixed actions, copy and settings dispatch");
} finally {
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
}
