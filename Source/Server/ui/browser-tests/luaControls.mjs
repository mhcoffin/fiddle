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
    // A backend stand-in: two layers and their scripts; it applies what the
    // UI sends (clamped, as LuaPlugin does) and pushes the mixer state back.
    const installFixture = (target) => target.evaluate(() => {
        window.fixtureMessages = [];
        window.fixtureLua = [{
            index: 0, name: "Humanize", fileName: "humanize.lua", filePath: "/plugins/humanize.lua",
            loaded: true, enabled: true, fileStatus: "unchanged", description: "Adds subtle randomisation.",
            paramErrors: [], paramNotes: [],
            params: [
                { name: "timing", label: "Timing", type: "float", min: 0, max: 50, step: 0.5, unit: "ms", control: "knob", default: 15.25, value: 22.5 },
                { name: "beat", label: "Only on the beat", type: "bool", default: false, value: false },
                { name: "feel", label: "Feel", type: "enum", options: ["Tight", "Loose"], default: "Tight", value: "Loose" },
                { name: "seed", label: "Variation", type: "int", min: 0, max: 99999, step: 1, control: "field", zeroLabel: "Auto", randomize: true, default: 0, value: 0 },
                { name: "velocity", label: "Velocity", type: "int", min: 0, max: 40, step: 1, control: "knob", default: 8, value: 8 },
                { name: "rate", label: "Rate", type: "int", min: 40, max: 240, step: 1, unit: "BPM", control: "knob", default: 120, value: 120 },
            ],
        }];
        window.fixtureLua2 = [{
            index: 0, name: "Shaper", fileName: "shaper.lua", filePath: "/plugins/shaper.lua",
            loaded: true, enabled: true, fileStatus: "unchanged", description: "", paramErrors: [], paramNotes: [],
            params: [{ name: "seed", label: "Variation", type: "int", min: 0, max: 99999, step: 1, control: "field", default: 0, value: 7 }],
        }];
        window.fixtureLayers = ["layer-1", "layer-2"];
        const scriptsOf = (id) => id === "layer-1" ? window.fixtureLua : window.fixtureLua2;
        const strip = (id, index) => ({
            id, chairId: `chair-${index + 1}`, chairBusId: `chair:chair-${index + 1}`, inputPort: 0, inputChannel: index,
            library: "Strings", layerName: index === 0 ? "Violin I" : "Viola", family: "strings", isSolo: false, active: true,
            gainDb: 0, muted: false, soloed: false, pluginUid: 0, hasPlugin: false,
            luaPlugins: structuredClone(scriptsOf(id)),
        });
        window.pushStrips = () => window.__dispatchFromCpp({ type: "setMixerState",
            data: window.fixtureLayers.map((id) => strip(id, id === "layer-1" ? 0 : 1)) });
        window.__JUCE__ = { backend: { emitEvent: (_event, request) => {
            const message = request.params[0];
            window.fixtureMessages.push(message);
            const plugin = scriptsOf(message.payload?.[0])?.[message.payload?.[1]];
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
        window.__dispatchFromCpp({ type: "setChairState", data: ["Violin I", "Viola"].map((name, index) => ({
            id: `chair-${index + 1}`, name, family: "strings", role: "section", port: 0, channel: index,
            ordinal: index + 1, displayOrder: index, flatIndex: index })) });
        window.pushStrips();
    });
    const open = async (target) => {
        await target.goto(`http://127.0.0.1:${server.address().port}/`);
        await target.waitForSelector(".mixer-container");
        await installFixture(target);
        await target.waitForSelector(".channel-strip");
    };
    await open(page);
    await page.locator(".ch-details-btn").first().click();
    const details = page.getByRole("dialog", { name: /Layer details/ });
    await details.waitFor();
    const card = details.getByRole("article", { name: "Humanize" });
    const messages = () => page.evaluate(() => window.fixtureMessages.filter((m) => m.type !== "requestMixerState"));
    const clear = () => page.evaluate(() => { window.fixtureMessages = []; });
    const sent = (type, ...payload) => ({ type, payload });
    const single = (type, ...payload) => [sent("beginHistoryGesture"), sent(type, ...payload), sent("endHistoryGesture")];

    // Knobs line up whatever their units, and fit their card.
    const linedUp = async (where) => {
        const right = (name) => where.getByRole("textbox", { name }).evaluate((e) => e.getBoundingClientRect().right);
        const edges = [await right("Humanize: Velocity"), await right("Humanize: Timing in ms"), await right("Humanize: Rate in BPM")];
        assert.ok(Math.max(...edges) - Math.min(...edges) < 1, `knob fields line up: ${edges}`);
        const article = where.getByRole("article", { name: "Humanize" });
        assert.ok(await article.evaluate((e) => e.scrollWidth <= e.clientWidth), "the card doesn't overflow");
    };
    await linedUp(details);

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

    // Double-clicking the knob restores the exact default, off the step grid,
    // as one gesture.
    await clear();
    await card.getByRole("slider", { name: "Humanize: Timing" }).dblclick();
    const knob = await messages();
    assert.deepEqual(knob.at(-1), sent("endHistoryGesture"));
    assert.deepEqual(knob.filter((m) => m.type === "setLuaParam").at(-1), sent("setLuaParam", "layer-1", 0, "timing", 15.25));

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

    // The strip's MIDI FX band opens the same controls in a floating panel.
    await details.getByRole("button", { name: "Close", exact: true }).click();
    await details.waitFor({ state: "detached" });
    const band = page.getByRole("region", { name: "MIDI FX" }).first();
    await band.getByRole("button", { name: /MIDI FX for Violin I/ }).click();
    const panel = page.getByRole("dialog", { name: "MIDI FX for Violin I" });
    await panel.waitFor();
    await linedUp(panel);
    // It stays open while the user works elsewhere in the mixer.
    await page.locator(".channel-strip .ch-strip-identity").first().click();
    assert.equal(await panel.isVisible(), true);
    await clear();
    await panel.getByRole("switch", { name: "Humanize on" }).click();
    assert.deepEqual(await messages(), [sent("setLuaEnabled", "layer-1", 0, true)]);
    // Its header moves it, and Escape closes it.
    const before = await panel.boundingBox();
    const header = panel.locator("header").first();
    const start = await header.boundingBox();
    await page.mouse.move(start.x + 40, start.y + 12);
    await page.mouse.down();
    await page.mouse.move(start.x + 40 - 120, start.y + 12 + 60, { steps: 4 });
    await page.mouse.up();
    const after = await panel.boundingBox();
    assert.ok(Math.abs(after.x - (before.x - 120)) < 2 && Math.abs(after.y - (before.y + 60)) < 2, "the header drags the panel");
    await page.keyboard.press("Escape");
    await panel.waitFor({ state: "detached" });

    // Clicking another layer's band shows that layer, with fresh controls:
    // a value half-typed for Violin I never reaches Viola.
    await band.getByRole("button", { name: /MIDI FX for Violin I/ }).click();
    await panel.waitFor();
    const halfTyped = panel.getByRole("textbox", { name: "Humanize: Variation" });
    await halfTyped.fill("999");
    const violaBand = page.getByRole("region", { name: "MIDI FX" }).nth(1);
    await clear();
    await violaBand.getByRole("button", { name: /MIDI FX for Viola/ }).click();
    const violaPanel = page.getByRole("dialog", { name: "MIDI FX for Viola" });
    await violaPanel.waitFor();
    assert.equal(await violaPanel.getByRole("textbox", { name: "Shaper: Variation" }).inputValue(), "7");
    assert.equal((await messages()).some((m) => m.type === "setLuaParam" && m.payload[0] === "layer-2"), false);
    // Layer details closes it.
    await page.locator(".ch-details-btn").nth(1).click();
    await details.waitFor();
    assert.equal(await violaPanel.count(), 0);
    await details.getByRole("button", { name: "Close", exact: true }).click();
    // It closes when its layer goes (Undo, a branch switch), and Escape then
    // reaches the mixer again.
    await violaBand.getByRole("button", { name: /MIDI FX for Viola/ }).click();
    await violaPanel.waitFor();
    await page.evaluate(() => { window.fixtureLayers = ["layer-1"]; window.pushStrips(); });
    await violaPanel.waitFor({ state: "detached" });
    await page.evaluate(() => { window.fixtureLayers = ["layer-1", "layer-2"]; window.pushStrips(); });
    await page.waitForTimeout(100);
    assert.equal(await violaPanel.count(), 0, "the panel doesn't come back with its layer");

    // A layer without scripts shows an empty band; it opens the panel with
    // the catalog.
    await page.evaluate(() => {
        window.fixtureLua = [];
        window.__dispatchFromCpp({ type: "setLuaPluginCatalog", data: [{ name: "Humanize", filePath: "/plugins/humanize.lua" }] });
        window.pushStrips();
    });
    await band.getByRole("button", { name: /MIDI FX for Violin I — none/ }).click();
    await panel.waitFor();
    assert.match(await panel.innerText(), /No MIDI FX on this layer/);
    await clear();
    await panel.getByRole("combobox", { name: "Add a script" }).selectOption("/plugins/humanize.lua");
    assert.deepEqual(await messages(), [sent("addStripLuaPlugin", "layer-1", "/plugins/humanize.lua")]);

    await page.keyboard.press("Escape");

    // At the Large size the band lists the scripts, each with Bypass; a name
    // opens the panel on that script.
    const large = await (await browser.newContext({ viewport: { width: 1280, height: 900 } })).newPage();
    large.setDefaultTimeout(10000);
    large.on("pageerror", error => errors.push(error.message));
    await large.addInitScript(() => localStorage.setItem("fiddle.mixer.stripSize", "large"));
    await open(large);
    const largeBand = large.getByRole("region", { name: "MIDI FX" }).first();
    await largeBand.getByRole("button", { name: "Bypass" }).click();
    assert.deepEqual(await large.evaluate(() => window.fixtureMessages.filter((m) => m.type === "setLuaEnabled")),
        [sent("setLuaEnabled", "layer-1", 0, false)]);
    await largeBand.getByRole("button", { name: "Enable" }).waitFor();
    await largeBand.getByRole("button", { name: "Humanize" }).click();
    const largePanel = large.getByRole("dialog", { name: "MIDI FX for Violin I" });
    await largePanel.waitFor();
    assert.ok(await largePanel.locator(".lua-card.highlighted").count() === 1, "the chosen script is highlighted");
    await large.close();

    assert.deepEqual(errors, [], "no browser runtime errors");
    console.log("PASS: Lua script controls send checked values, pair history gestures and follow the backend; the MIDI FX band opens them in a floating panel");
} finally {
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
}
