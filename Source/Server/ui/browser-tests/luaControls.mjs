// Lua script controls (docs/lua-scripts.md) against the built UI: what each
// control sends, how it pairs history gestures, and how it follows the
// backend. Build the UI first, then run with Node. Requires Playwright and
// Chrome (or set FIDDLE_TEST_BROWSER_CHANNEL); FIDDLE_PLAYWRIGHT_MODULE may
// point to an externally installed Playwright. No live Fiddle state.
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
    const page = await (await browser.newContext({ viewport: { width: 1280, height: 900 } })).newPage();
    page.setDefaultTimeout(10000);
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.waitForSelector(".mixer-container");

    // A backend stand-in: it keeps the layer's scripts, applies what the UI
    // sends (clamped, as LuaPlugin does), and pushes the mixer state back.
    await page.evaluate(() => {
        window.fixtureMessages = [];
        window.fixtureLua = [{
            index: 0, name: "Humanize", fileName: "humanize.lua", filePath: "/plugins/humanize.lua",
            loaded: true, enabled: true, fileStatus: "unchanged", description: "Adds subtle randomisation.",
            paramErrors: [], paramNotes: [],
            params: [
                { name: "timing", label: "Timing", type: "float", min: 0, max: 50, step: 0.5, unit: "ms", control: "knob", default: 15, value: 22.25 },
                { name: "beat", label: "Only on the beat", type: "bool", default: false, value: false },
                { name: "feel", label: "Feel", type: "enum", options: ["Tight", "Loose"], default: "Tight", value: "Loose" },
                { name: "seed", label: "Variation", type: "int", min: 0, max: 99999, step: 1, control: "field", zeroLabel: "Auto", randomize: true, default: 0, value: 0 },
            ],
        }];
        const strip = () => ({
            id: "layer-1", chairId: "chair-1", chairBusId: "chair:chair-1", inputPort: 0, inputChannel: 0,
            library: "Strings", layerName: "Violin I", family: "strings", isSolo: false, active: true,
            gainDb: 0, muted: false, soloed: false, pluginUid: 0, hasPlugin: false,
            luaPlugins: structuredClone(window.fixtureLua),
        });
        window.pushStrips = () => window.__dispatchFromCpp({ type: "setMixerState", data: [strip()] });
        window.__JUCE__ = { backend: { emitEvent: (_event, request) => {
            const message = request.params[0];
            window.fixtureMessages.push(message);
            const plugin = window.fixtureLua[message.payload?.[1]];
            if (message.type === "setLuaParam" && plugin) {
                const param = plugin.params.find((p) => p.name === message.payload[2]);
                let value = message.payload[3];
                if (param.type === "int" || param.type === "float") value = Math.min(param.max, Math.max(param.min, value));
                param.value = value;
                queueMicrotask(window.pushStrips);
            }
            if (message.type === "setLuaEnabled" && plugin) {
                plugin.enabled = message.payload[2];
                queueMicrotask(window.pushStrips);
            }
        } } };
        window.__dispatchFromCpp({ type: "setChairState", data: [{ id: "chair-1", name: "Violin I", family: "strings",
            role: "section", port: 0, channel: 0, ordinal: 1, displayOrder: 0, flatIndex: 0 }] });
        window.pushStrips();
    });
    await page.locator(".ch-details-btn").first().click();
    const details = page.getByRole("dialog", { name: /Layer details/ });
    await details.waitFor();
    const card = details.getByRole("article", { name: "Humanize" });
    const messages = () => page.evaluate(() => window.fixtureMessages.filter((m) => m.type !== "requestMixerState"));
    const clear = () => page.evaluate(() => { window.fixtureMessages = []; });
    const sent = (type, ...payload) => ({ type, payload });
    const single = (type, ...payload) => [sent("beginHistoryGesture"), sent(type, ...payload), sent("endHistoryGesture")];

    // The field: Auto for 0, typed values clamped, a refused one put back.
    const field = card.getByRole("textbox", { name: "Humanize: Variation" });
    assert.equal(await field.inputValue(), "Auto");
    await clear();
    await field.fill("-5");
    await field.press("Enter");
    assert.deepEqual(await messages(), [], "-5 clamps to 0, which is already the value");
    assert.equal(await field.inputValue(), "Auto");
    await field.fill("123");
    await field.press("Enter");
    assert.deepEqual(await messages(), single("setLuaParam", "layer-1", 0, "seed", 123));
    await page.waitForFunction(() => document.activeElement?.tagName !== "INPUT");
    assert.equal(await field.inputValue(), "123");
    // Escape cancels the edit and leaves Layer details open.
    await field.fill("9");
    await field.press("Escape");
    assert.equal(await details.isVisible(), true);
    assert.equal(await field.inputValue(), "123");
    // A value from the backend (Undo) shows, unless the user is typing.
    await page.evaluate(() => { window.fixtureLua[0].params[3].value = 0; window.pushStrips(); });
    await page.waitForFunction(() => [...document.querySelectorAll("input")].some((i) => i.value === "Auto"));
    await clear();
    await card.getByRole("button", { name: "New" }).click();
    const [, randomSeed] = await messages();
    assert.equal(randomSeed.type, "setLuaParam");
    assert.ok(Number.isInteger(randomSeed.payload[3]) && randomSeed.payload[3] >= 1);

    // Double-clicking the knob restores the exact default, as one gesture.
    await clear();
    await card.getByRole("slider", { name: "Humanize: Timing" }).dblclick();
    const knob = await messages();
    assert.deepEqual(knob.at(-1), sent("endHistoryGesture"));
    assert.deepEqual(knob.filter((m) => m.type === "setLuaParam").at(-1), sent("setLuaParam", "layer-1", 0, "timing", 15));

    // The blue dot resets one value; choices and switches send their value.
    await clear();
    await card.getByRole("button", { name: "Reset Feel to Tight" }).click();
    assert.deepEqual(await messages(), single("setLuaParam", "layer-1", 0, "feel", "Tight"));
    await clear();
    await card.getByRole("switch", { name: "Humanize: Only on the beat" }).click();
    assert.deepEqual(await messages(), single("setLuaParam", "layer-1", 0, "beat", true));
    await clear();
    await card.getByRole("switch", { name: "Humanize on" }).click();
    assert.deepEqual(await messages(), [sent("setLuaEnabled", "layer-1", 0, false)]);
    await details.getByRole("article", { name: "Humanize (bypassed)" }).waitFor();

    assert.deepEqual(errors, [], "no browser runtime errors");
    console.log("PASS: Lua script controls send checked values, pair history gestures and follow the backend");
} finally {
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
}
