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
    const context = await browser.newContext({ viewport: { width: 1280, height: 900 } });
    const page = await context.newPage();
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
        window.fixtureChairs = chairs;
        window.fixtureMessages = [];
        window.__JUCE__ = { backend: { emitEvent: (_event, request) => {
            const message = request.params[0];
            window.fixtureMessages.push(message);
            if (message.type === "setStripBalance") {
                const [stripId, balance] = message.payload;
                const strip = window.fixtureStrips.find(s => s.id === stripId);
                strip.audio = { ...strip.audio, balance };
                queueMicrotask(() => window.__dispatchFromCpp({ type: "setStripAudioState", data: { stripId, balance } }));
            }
            if (message.type === "setGroupBusBalance") {
                const [id, balance] = message.payload;
                queueMicrotask(() => window.__dispatchFromCpp({ type: "setGroupBusState", data: [
                    { id, name: "Strings", gainDb: 0, audio: { balance } },
                ] }));
            }
            if (message.type === "updateStripSend") {
                const [stripId, id, busId, gainDb, enabled] = message.payload;
                const strip = window.fixtureStrips.find((s) => s.id === stripId);
                if (strip?.sends) {
                    strip.sends = strip.sends.map((s) => s.id === id ? { ...s, id, busId, gainDb, enabled } : s);
                    queueMicrotask(() => window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips }));
                }
            }
            if (message.type === "setStripSendVisible") {
                const [stripId, id, showOnStrip] = message.payload;
                const strip = window.fixtureStrips.find((s) => s.id === stripId);
                strip.sends = strip.sends.map((s) => s.id === id ? { ...s, showOnStrip } : s);
                queueMicrotask(() => window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips }));
            }
            if (message.type === "setStripSendPreFader") {
                const [stripId, id, preFader] = message.payload;
                const strip = window.fixtureStrips.find((s) => s.id === stripId);
                strip.sends = strip.sends.map((s) => s.id === id ? { ...s, preFader } : s);
                queueMicrotask(() => window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips }));
            }
            if (message.type === "showView" && ["inspector", "audio-bank", "chairs", "bus-manager", "audio-performance"].includes(message.payload[0]))
                queueMicrotask(() => window.__dispatchFromCpp({ type: "showMixerView", data: message.payload[0] }));
        } } };
        window.__dispatchFromCpp({ type: "setPluginList", data: [
            { uid: 101, name: "A very long sample player name", valid: true },
            { uid: 102, name: "Another player", valid: true },
            { uid: 103, name: "Invalid player", valid: false },
        ] });
        window.__dispatchFromCpp({ type: "setChairState", data: chairs });
        window.__dispatchFromCpp({ type: "setMixerState", data: strips });
        window.__dispatchFromCpp({ type: "setGroupBusState", data: [
            { id: "bus-strings", name: "Strings", gainDb: 0, muted: false, soloed: false },
        ] });
    });
    await page.waitForSelector(".channel-strip");
    const balanceInput = page.getByRole("textbox", { name: "Flute 1 stereo balance", exact: true });
    assert.equal(await balanceInput.inputValue(), "C");
    await balanceInput.fill("L25");
    await balanceInput.press("Enter");
    assert.equal(await balanceInput.inputValue(), "L25");
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.slice(-3)), [
        { type: "beginHistoryGesture", payload: [] },
        { type: "setStripBalance", payload: ["chair-0-0", -0.25] },
        { type: "endHistoryGesture", payload: [] },
    ]);
    await balanceInput.fill("oops");
    await balanceInput.press("Enter");
    assert.equal(await balanceInput.inputValue(), "L25");
    await page.getByRole("button", { name: "Center Flute 1 stereo balance", exact: true }).click();
    assert.equal(await balanceInput.inputValue(), "C");
    const balanceKnob = page.getByRole("slider", { name: "Flute 1 stereo balance", exact: true });
    assert.equal(await balanceKnob.locator("svg path").count(), 0, "balance has no indicator ring");
    assert.equal(await balanceKnob.locator("svg line").count(), 1, "balance retains its needle");
    await balanceKnob.focus();
    await page.keyboard.press("End");
    assert.equal(await balanceInput.inputValue(), "R100");
    await page.evaluate(() => window.__dispatchFromCpp({ type: "setStripAudioState", data: { stripId: "chair-0-0", balance: -0.4 } }));
    assert.equal(await balanceInput.inputValue(), "L40", "undo/native state updates the control");
    const busBalance = page.getByRole("textbox", { name: "Strings stereo balance", exact: true });
    await busBalance.fill("50R");
    await busBalance.press("Enter");
    assert.equal(await busBalance.inputValue(), "R50");
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.slice(-3)[1]),
        { type: "setGroupBusBalance", payload: ["bus-strings", 0.5] });
    // Exercise send controls through the real built Svelte UI, echoing model
    // snapshots just as the native backend does after successful commands.
    await page.locator(".ch-audio-fx").first().click();
    const channelAudio = page.getByRole("dialog", { name: /Channel Audio for/ });
    await channelAudio.getByLabel("New send destination", { exact: true }).selectOption("bus-strings");
    await channelAudio.getByRole("button", { name: "Add send", exact: true }).click();
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
        { type: "addStripSend", payload: ["chair-0-0", "bus-strings"] });
    await page.evaluate(() => {
        window.fixtureStrips[0].sends = [{ id: "test-send", busId: "bus-strings", gainDb: -12, enabled: true }];
        window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips });
    });
    const sendRow = channelAudio.locator(".send-row");
    assert.equal(await sendRow.locator(".knob svg path.value").count(), 1, "send level retains its indicator ring");
    await sendRow.getByRole("checkbox", { name: "Show on strip" }).check();
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
        { type: "setStripSendVisible", payload: ["chair-0-0", "test-send", true] });
    assert.equal(await page.locator(".promoted-send").count(), 1);
    assert.equal(await sendRow.getByLabel("Send tap", { exact: true }).inputValue(), "postFader");
    await sendRow.getByLabel("Send tap", { exact: true }).selectOption("preFader");
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
        { type: "setStripSendPreFader", payload: ["chair-0-0", "test-send", true] });
    assert.equal(await page.locator('.ch-fx-section:not(.post-fader) .promoted-send').count(), 1);
    assert.equal(await page.locator('.post-fader .promoted-send').count(), 0);
    assert.equal(await sendRow.getByLabel("Send level in dB").inputValue(), "-12.0");
    // An authoritative Undo snapshot moves the shortcut back without changing its level.
    await page.evaluate(() => {
        window.fixtureStrips[0].sends[0].preFader = false;
        window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips });
    });
    assert.equal(await sendRow.getByLabel("Send tap", { exact: true }).inputValue(), "postFader");
    assert.equal(await page.locator('.post-fader .promoted-send').count(), 1);
    await sendRow.getByRole("checkbox", { name: "Show on strip" }).uncheck();
    assert.equal(await page.locator(".promoted-sends").count(), 0);
    assert.equal(await sendRow.getByRole("checkbox", { name: "Enabled", exact: true }).isChecked(), true,
        "hiding a shortcut must not disable its send");
    await sendRow.getByRole("checkbox", { name: "Show on strip" }).check();
    await sendRow.getByLabel("Send level in dB").fill("-6");
    await sendRow.getByLabel("Send level in dB").press("Tab");
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.filter((m) => m.type === "updateStripSend").at(-1)),
        { type: "updateStripSend", payload: ["chair-0-0", "test-send", "bus-strings", -6, true] });
    const sendKnob = sendRow.getByRole("slider", { name: "Send level", exact: true });
    assert.equal(await sendKnob.getAttribute("aria-valuenow"), "-6");
    await page.evaluate(() => { window.fixtureMessages = []; });
    const knobBox = await sendKnob.boundingBox();
    const knobX = knobBox.x + knobBox.width / 2, knobY = knobBox.y + knobBox.height / 2;
    await page.mouse.move(knobX, knobY);
    await page.mouse.down();
    await page.mouse.move(knobX, knobY - 12, { steps: 4 });
    await new Promise(resolve => setTimeout(resolve, 600)); // exceeds the backend's normal coalescing window
    await page.mouse.move(knobX, knobY - 24, { steps: 4 });
    await page.mouse.up();
    const dragMessages = await page.evaluate(() => window.fixtureMessages);
    assert.equal(dragMessages.filter((m) => m.type === "beginHistoryGesture").length, 1);
    assert.equal(dragMessages.filter((m) => m.type === "endHistoryGesture").length, 1);
    assert.equal(dragMessages.at(-1).type, "endHistoryGesture", "flush final value before closing Undo gesture");
    const normalGain = Number(await sendKnob.getAttribute("aria-valuenow"));
    assert.ok(normalGain > -6);
    // Undo/backend snapshots update both knob and readout.
    const resetSendGain = async (gainDb) => page.evaluate((gainDb) => {
        window.fixtureStrips[0].sends[0].gainDb = gainDb;
        window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips });
    }, gainDb);
    await resetSendGain(-6);
    await page.keyboard.down("Shift");
    await page.mouse.move(knobX, knobY); await page.mouse.down();
    await page.mouse.move(knobX, knobY - 24, { steps: 4 }); await page.mouse.up();
    await page.keyboard.up("Shift");
    const fineGain = Number(await sendKnob.getAttribute("aria-valuenow"));
    assert.ok(fineGain > -6 && fineGain < normalGain);
    await resetSendGain(-6);
    await sendKnob.focus();
    await page.keyboard.press("ArrowUp");
    assert.equal(await sendKnob.getAttribute("aria-valuenow"), "-5.5");
    await page.keyboard.press("Shift+ArrowDown");
    assert.equal(await sendKnob.getAttribute("aria-valuenow"), "-5.6");
    await page.keyboard.press("Home");
    assert.equal(await sendRow.getByLabel("Send level in dB").inputValue(), "−∞");
    await page.keyboard.press("End");
    assert.equal(await sendKnob.getAttribute("aria-valuenow"), "12");
    const sendText = sendRow.getByLabel("Send level in dB");
    await sendText.fill("invalid"); await sendText.press("Enter");
    assert.equal(await sendKnob.getAttribute("aria-valuenow"), "12");
    await sendText.fill("-30"); await sendText.press("Escape");
    assert.equal(await channelAudio.isVisible(), true);
    assert.equal(await sendKnob.getAttribute("aria-valuenow"), "12");
    await sendText.fill("-inf"); await sendText.press("Enter");
    assert.equal(await sendKnob.getAttribute("aria-valuenow"), "-120");
    await sendText.fill("-6"); await sendText.press("Enter");
    await page.evaluate(() => {
        window.fixtureStrips[0].sends[0].gainDb = -6;
        window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips });
    });
    await sendRow.getByRole("checkbox", { name: "Enabled" }).uncheck();
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
        { type: "updateStripSend", payload: ["chair-0-0", "test-send", "bus-strings", -6, false] });
    // Snapshot echo is authoritative, including undo/redo and deleted buses.
    await page.evaluate(() => {
        window.fixtureStrips[0].sends[0].enabled = false;
        window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips });
    });
    assert.equal(await sendRow.getByRole("checkbox", { name: "Enabled" }).isChecked(), false);
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "post-fader-sends.png") });
    await sendRow.getByRole("button", { name: "Remove", exact: true }).click();
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
        { type: "removeStripSend", payload: ["chair-0-0", "test-send"] });
    await page.evaluate(() => {
        window.fixtureStrips[0].sends = [];
        window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips });
    });
    assert.equal(await sendRow.count(), 0);
    assert.equal(await page.locator(".promoted-sends").count(), 0, "no reserved band without promoted sends");
    await channelAudio.getByRole("button", { name: "Close", exact: true }).click();
    const viewButton = page.getByRole("button", { name: "View", exact: true });
    for (const width of [800, 1280]) {
        await page.setViewportSize({ width, height: 900 });
        const toolbar = await page.locator(".mixer-toolbar").evaluate(element => {
            const box = element.getBoundingClientRect();
            const controls = [...element.querySelectorAll("button")].map(button => button.getBoundingClientRect()).filter(rect => rect.width > 0 && rect.height > 0);
            return { height: box.height, right: box.right, controls: controls.map(r => ({ top: r.top, bottom: r.bottom, right: r.right })) };
        });
        assert.ok(toolbar.height <= 50, "toolbar stays in a single compact row");
        assert.ok(toolbar.controls.every(r => r.right <= toolbar.right), "toolbar controls fit the window");
        assert.ok(Math.max(...toolbar.controls.map(r => r.top)) < Math.min(...toolbar.controls.map(r => r.bottom)), "all toolbar buttons share the same row");
    }
    await viewButton.click();
    await page.getByRole("checkbox", { name: "Show library bar" }).uncheck();
    assert.equal(await page.locator(".library-chip-bar").count(), 0);
    assert.equal(await page.evaluate(() => localStorage.getItem("fiddle.mixer.showLibraryBar")), "false");
    // A new page uses the saved visibility preference.
    const reopened = await page.context().newPage();
    await reopened.goto(page.url());
    await reopened.getByRole("button", { name: "View", exact: true }).click();
    assert.equal(await reopened.getByRole("checkbox", { name: "Show library bar" }).isChecked(), false);
    await reopened.close();
    await page.getByRole("checkbox", { name: "Show library bar" }).check();
    await page.keyboard.press("Escape");
    assert.equal(await viewButton.getAttribute("aria-expanded"), "false");
    await viewButton.click();
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "toolbar-view.png") });
    await page.getByRole("button", { name: "Zoom in", exact: true }).click();
    assert.equal(await page.getByRole("button", { name: "Reset zoom", exact: true }).innerText(), "110%");
    await page.getByRole("button", { name: "Reset zoom", exact: true }).click();
    await page.getByRole("button", { name: "Library Manager", exact: true }).click();
    assert.equal(await viewButton.getAttribute("aria-expanded"), "false");
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
        { type: "showView", payload: ["library-manager"] });
    const audioBankToggle = page.locator(".bank .header-toggle");
    await audioBankToggle.click();
    assert.equal(await audioBankToggle.getAttribute("aria-expanded"), "false");
    await viewButton.click();
    await page.getByRole("button", { name: "Audio Buses & Master", exact: true }).click();
    assert.equal(await audioBankToggle.getAttribute("aria-expanded"), "true");
    await viewButton.click();
    await page.getByRole("button", { name: "Layer Inspector", exact: true }).click();
    await page.getByRole("button", { name: "Close inspector", exact: true }).click();
    const sizes = [];
    for (const size of ["compact", "comfortable", "large"]) {
        await viewButton.click();
        await page.locator("#strip-size-select").selectOption(size);
        await page.keyboard.press("Escape");
        await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
        const measured = await page.evaluate(() => ({
            scrollWidth: document.querySelector(".console").scrollWidth,
            sectionTabHeight: document.querySelector(".folder-tab:not(.folder-tab-collapsed)").getBoundingClientRect().height,
            audioTabHeight: document.querySelector(".bank > .header").getBoundingClientRect().height,
            audioHeaderHeights: [...document.querySelectorAll(".bank .bus-header")].map(header => header.getBoundingClientRect().height),
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
                    faderBottoms: [...group.querySelectorAll(".fader-track")].map(track => track.getBoundingClientRect().bottom),
                    buttonFits: (() => {
                        const button = group.querySelector(".bridge-add-layer").getBoundingClientRect();
                        return button.left >= rect.left && button.right <= rect.right;
                    })(),
                };
            }),
            audioFaders: [...document.querySelectorAll(".bank .channel")].map(channel => {
                const track = channel.querySelector(".fader-track").getBoundingClientRect();
                return {
                    name: channel.closest(".bus-group").querySelector(".identity strong").textContent,
                    top: track.top,
                    bottom: track.bottom,
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
        assert.ok(Math.abs(measured.sectionTabHeight - measured.audioTabHeight) <= 1,
            `section tabs must match: ${measured.sectionTabHeight}, ${measured.audioTabHeight}`);
        assert.ok(Math.max(...heights, ...measured.audioHeaderHeights) - Math.min(...heights, ...measured.audioHeaderHeights) <= 1,
            `strip headers must match: ${[...heights, ...measured.audioHeaderHeights].join(", ")}`);
        const tops = measured.groups.flatMap(group => group.faderTops);
        const allTops = [...tops, ...measured.audioFaders.map(fader => fader.top)];
        const bottoms = measured.groups.flatMap(group => group.faderBottoms);
        const allBottoms = [...bottoms, ...measured.audioFaders.map(fader => fader.bottom)];
        console.log(`${size}: fader spans ${measured.audioFaders.map(fader => `${fader.name} ${fader.top}-${fader.bottom}`).join(", ")}; layers ${Math.min(...tops)}-${Math.max(...bottoms)}`);
        assert.ok(Math.max(...allTops) - Math.min(...allTops) <= 1,
            `all fader tops must align: ${allTops.join(", ")}`);
        if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR) {
            await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, `${size}.png`) });
        }
        assert.ok(Math.max(...allBottoms) - Math.min(...allBottoms) <= 1,
            `all fader bottoms must align: ${allBottoms.join(", ")}`);
        const firstChair = page.locator(".inst-group").first();
        const firstStrip = firstChair.locator(".channel-strip");
        const balanceFits = await firstStrip.evaluate(strip => {
            const rect = strip.getBoundingClientRect();
            return [...strip.querySelectorAll(".balance-control button, .balance-control input, .balance-control .knob")]
                .every(control => { const r = control.getBoundingClientRect(); return r.left >= rect.left && r.right <= rect.right; });
        });
        assert.ok(balanceFits, `${size}: balance controls fit the strip`);
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
            output: strip.querySelector(".ch-output-routing select").getBoundingClientRect().height,
        }));
        assert.equal(controlHeights.vst, 32);
        assert.equal(controlHeights.fx, 24, "FX management uses a compact header button");
        assert.equal(controlHeights.output, controlHeights.vst, "output and VSTi controls must have equal height");
        assert.deepEqual(await firstStrip.locator('.ch-section-title').allTextContents(),
            ['Instrument', 'Pre-fader', 'Post-fader', 'Output']);
        const headingsFit = await firstStrip.locator('.ch-section-heading').evaluateAll((headings) => headings.every((heading) => {
            const text = heading.querySelector('.ch-section-title').getBoundingClientRect();
            const button = heading.querySelector('button').getBoundingClientRect();
            return text.right <= button.left + 1;
        }));
        assert.ok(headingsFit, `${size}: labels and settings buttons do not overlap`);

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
    // Uneven promoted-send counts must never offset layer, chair, bus or Master
    // faders. Long lists scroll within the band rather than taking more height.
    await page.evaluate(() => {
        const buses = ["bus-strings", "bus-hall", "bus-delay", "bus-pre-a", "bus-pre-b", "bus-pre-c"].map((id, i) => ({
            id, name: i === 0 ? "Strings" : i === 1 ? "A very long hall reverb destination" : "Delay", gainDb: 0,
        }));
        window.__dispatchFromCpp({ type: "setGroupBusState", data: buses });
        window.fixtureStrips[0].sends = buses.map((bus, i) => ({
            id: `promoted-${i}`, busId: bus.id, gainDb: -12, enabled: i !== 1, showOnStrip: true, preFader: i >= 3,
        }));
        window.fixtureStrips[0].audio = {
            insertCount: 2,
            preFaderInserts: [{ id: "pre-eq", name: "Pre EQ", status: "loaded" }],
            postFaderInserts: [{ id: "post-comp", name: "Post compressor", status: "loaded" }],
        };
        window.fixtureStrips[1].audio = {
            preFaderInserts: Array.from({ length: 12 }, (_, i) => ({ id: `bypassed-${i}`, name: `Bypassed FX ${i}`, status: 'loaded', bypassed: true })),
        };
        window.fixtureStrips[2].audio = {
            preFaderInserts: [{ id: 'missing-fx', name: 'Missing FX', status: 'missing', bypassed: false }],
        };
        window.fixtureStrips.find((s) => s.id === "chair-8-0").sends = [
            { id: "one-send", busId: buses[0].id, gainDb: -18, enabled: true, showOnStrip: true, preFader: true },
        ];
        window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips });
    });
    for (const size of ["compact", "comfortable", "large"]) {
        await viewButton.click(); await page.locator("#strip-size-select").selectOption(size);
        await page.keyboard.press("Escape");
        const geometry = await page.evaluate(() => {
            const tracks = [...document.querySelectorAll(".fader-track")].map((e) => e.getBoundingClientRect());
            const sends = document.querySelector(".post-fader .promoted-sends");
            const strip = document.querySelector(".channel-strip");
            const pre = strip.querySelector('.ch-fx-section:not(.post-fader)');
            const post = strip.querySelector('.ch-fx-section.post-fader');
            const label = sends.querySelector('.destination').getBoundingClientRect();
            const knob = sends.querySelector('.knob-control').getBoundingClientRect();
            const dial = sends.querySelector('.knob').getBoundingClientRect();
            const readout = sends.querySelector('.readout').getBoundingClientRect();
            return { tops: tracks.map((r) => r.top), bottoms: tracks.map((r) => r.bottom),
                bandHeight: sends.clientHeight, scrollHeight: sends.scrollHeight,
                ordered: strip.querySelector('.ch-instrument-summary').getBoundingClientRect().bottom <= pre.getBoundingClientRect().top
                    && pre.getBoundingClientRect().bottom <= post.getBoundingClientRect().top
                    && post.getBoundingClientRect().bottom <= strip.querySelector('.ch-output-routing').getBoundingClientRect().top,
                sendsInPost: post.contains(sends),
                preSendCount: pre.querySelectorAll('.promoted-send').length,
                preBandHeight: pre.querySelector('.promoted-sends').clientHeight,
                horizontal: label.right <= knob.left && label.bottom > knob.top,
                horizontalReadout: dial.right <= readout.left && Math.abs((dial.top + dial.bottom - readout.top - readout.bottom) / 2) <= 1,
                dialWidth: dial.width,
                arrow: sends.querySelector('.send-arrow')?.textContent,
                sectionTops: [...document.querySelectorAll('.channel-strip')].map((s) => [
                    s.querySelector('.ch-fx-section').getBoundingClientRect().top,
                    s.querySelector('.post-fader').getBoundingClientRect().top,
                    s.querySelector('.ch-output-routing').getBoundingClientRect().top,
                ]),
                fieldsFit: [...document.querySelectorAll(".promoted-sends .knob-control")].every((control) => {
                    const a = control.getBoundingClientRect(), b = control.closest(".promoted-sends").getBoundingClientRect();
                    return a.left >= b.left - 1 && a.right <= b.right + 1;
                }) };
        });
        assert.ok(Math.max(...geometry.tops) - Math.min(...geometry.tops) <= 1, `${size}: promoted fader tops align`);
        assert.ok(Math.max(...geometry.bottoms) - Math.min(...geometry.bottoms) <= 1, `${size}: promoted fader bottoms align`);
        assert.equal(geometry.bandHeight, size === "large" ? 72 : 104);
        assert.ok(geometry.ordered, `${size}: VSTi, pre FX, post FX, output order`);
        assert.ok(geometry.sendsInPost, `${size}: promoted sends are inside post-fader effects`);
        assert.equal(geometry.preSendCount, 3, `${size}: pre-fader sends are in the pre-fader section`);
        assert.equal(geometry.preBandHeight, geometry.bandHeight);
        assert.equal(geometry.horizontal, size === "large", `${size}: use horizontal send layout when space permits`);
        assert.ok(geometry.horizontalReadout, `${size}: knob and readout are side by side`);
        assert.equal(geometry.dialWidth, 26);
        assert.equal(geometry.arrow, "→");
        const headings = page.locator('.channel-strip .ch-fx-section:not(.post-fader) .ch-section-heading');
        assert.equal(await headings.nth(0).locator('.ch-insert-count').innerText(), size === 'compact' ? '1' : '1 FX');
        assert.equal(await headings.nth(1).locator('.ch-insert-count').innerText(), size === 'compact' ? '12' : '12 FX');
        assert.equal(await headings.nth(0).locator('.ch-insert-count.active').count(), 1);
        assert.equal(await headings.nth(1).locator('.ch-insert-count.active').count(), 0);
        assert.match(await headings.nth(1).locator('button').getAttribute('aria-label'), /12 inserts, all bypassed/);
        assert.equal(await headings.nth(2).locator('.ch-insert-count.active').count(), 0, 'missing is not active');
        assert.equal(await headings.nth(8).locator('.ch-insert-count').count(), 0, 'sends alone do not count as inserts');
        assert.ok(await headings.evaluateAll((rows) => rows.every((row) => {
            const text = row.querySelector('.ch-section-title').getBoundingClientRect();
            const button = row.querySelector('button').getBoundingClientRect();
            const bounds = row.closest('.ch-fx-section').getBoundingClientRect();
            const badge = row.querySelector('.ch-insert-count')?.getBoundingClientRect();
            return text.right <= button.left + 1 && button.right <= bounds.right + 1
                && (!badge || badge.right <= bounds.right + 1);
        })), `${size}: insert counts fit without overlapping headings`);
        for (let section = 0; section < 3; ++section) {
            const tops = geometry.sectionTops.map((s) => s[section]);
            assert.ok(Math.max(...tops) - Math.min(...tops) <= 1, `${size}: section ${section} aligns across layers`);
        }
        if (size === "large") {
            assert.equal(await page.locator('.ch-fx-section:not(.post-fader) .ch-fx-summary-name').first().innerText(), "Pre EQ");
            assert.equal(await page.locator('.post-fader .ch-fx-summary-name').first().innerText(), "Post compressor");
            await page.getByRole('button', { name: 'Edit Pre EQ', exact: true }).click();
            assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
                { type: 'toggleStripInsertEditor', payload: ['chair-0-0', 'pre-eq'] });
            await page.locator('.post-fader .ch-fx-summary-actions').first().getByRole('button', { name: 'Bypass', exact: true }).click();
            assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
                { type: 'setStripInsertBypassed', payload: ['chair-0-0', 'post-comp', true] });
            assert.ok(await page.locator('.ch-fx-summary').evaluateAll((summaries) => summaries.every((summary) => {
                const row = summary.querySelector('.ch-fx-summary-row');
                return !row || row.getBoundingClientRect().bottom <= summary.getBoundingClientRect().bottom + 1;
            })), "large: the first insert and its edit/bypass buttons fit without clipping");
        }
        assert.ok(geometry.scrollHeight > geometry.bandHeight);
        assert.ok(geometry.fieldsFit, `${size}: compact knob readouts fit`);
        if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
            await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, `promoted-${size}.png`) });
    }
    // Live rack updates (including Undo) update the indicator without a hover.
    await page.evaluate(() => window.__dispatchFromCpp({ type: 'setStripAudioState', data: {
        stripId: 'chair-0-0', preFaderInserts: [{ id: 'pre-eq', name: 'Pre EQ', status: 'loaded', bypassed: true }],
    } }));
    assert.equal(await page.locator('.channel-strip').first().locator('.ch-fx-section:not(.post-fader) .ch-insert-count.active').count(), 0);
    await page.evaluate(() => window.__dispatchFromCpp({ type: 'setStripAudioState', data: { stripId: 'chair-0-0', preFaderInserts: [] } }));
    assert.equal(await page.locator('.channel-strip').first().locator('.ch-fx-section:not(.post-fader) .ch-insert-count').count(), 0);
    const promoted = page.locator(".post-fader .promoted-sends").first();
    await promoted.getByRole("slider", { name: "Send to Strings", exact: true }).press("ArrowUp");
    assert.equal(await promoted.getByRole("slider", { name: "Send to Strings", exact: true }).getAttribute("aria-valuenow"), "-11.5");
    assert.equal(await promoted.locator(".promoted-send").count(), 3, "editing the gain preserves promotion");
    // A bus rename updates the promoted destination immediately.
    await page.evaluate(() => window.__dispatchFromCpp({ type: "setGroupBusState", data: [
        { id: "bus-strings", name: "Renamed return", gainDb: 0 },
        { id: "bus-hall", name: "Hall", gainDb: 0 }, { id: "bus-delay", name: "Delay", gainDb: 0 },
    ] }));
    assert.equal(await promoted.locator(".destination").first().innerText(), "Renamed return");
    await page.evaluate(() => {
        window.fixtureStrips.forEach((s) => { s.sends = []; delete s.audio; });
        window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips });
        window.__dispatchFromCpp({ type: "setGroupBusState", data: [
            { id: "bus-strings", name: "Strings", gainDb: 0, muted: false, soloed: false },
        ] });
    });
    const routedChair = page.locator(".inst-group").nth(8);
    for (const bar of await routedChair.locator(".select-bar-top").all())
        await bar.evaluate(element => element.dispatchEvent(new MouseEvent("click", {
            bubbles: true, metaKey: true,
        })));
    await page.evaluate(() => { window.fixtureMessages = []; });
    await routedChair.locator(".ch-output-routing select").first().selectOption("bus-strings");
    const routeMessage = await page.evaluate(() => window.fixtureMessages.at(-1));
    assert.equal(routeMessage.type, "setGroupStripDirectOutput");
    assert.deepEqual(JSON.parse(routeMessage.payload[0]), ["chair-8-0", "chair-8-1"]);
    assert.equal(routeMessage.payload[1], "bus-strings");
    await routedChair.locator(".select-bar-top").first().click();
    // Exercise real DOM gestures and bridge payloads, not source-text matches.
    await page.evaluate(() => {
        window.__dispatchFromCpp({ type: "setProjectSettings", data: {
            playbackDelayMs: 750, lockedChairIds: ["chair-8"],
        } });
        window.__dispatchFromCpp({ type: "setUndoState", data: {
            canUndo: true, canRedo: false, undoDescription: "Mute layer", redoDescription: "",
        } });
        window.fixtureMessages = [];
    });
    const layeredChair = page.locator(".inst-group").nth(8);
    assert.equal(await layeredChair.locator(".sum-lock-active").count(), 1);
    await layeredChair.locator(".channel-strip .mute-btn").first().click();
    const muteMessages = await page.evaluate(() => window.fixtureMessages.filter(x => x.type === "setMixerControls"));
    assert.equal(muteMessages.length, 1, "locked mute is a single native command");
    assert.equal(muteMessages[0].payload[0].length, 2, "command includes compensating sibling");
    assert.equal(muteMessages[0].payload[0].find(x => x.id === "chair-8-0").muted, true);
    assert.ok(Math.abs(muteMessages[0].payload[0].find(x => x.id === "chair-8-1").gainDb - 3.0103) < 0.01);
    await page.getByRole("button", { name: "Undo", exact: true }).click();
    assert.equal((await page.evaluate(() => window.fixtureMessages.at(-1))).type, "undo");
    assert.equal(await page.getByRole("button", { name: "Redo", exact: true }).isDisabled(), true);
    // Native state after Undo must replace optimistic gain shadows.
    await page.evaluate(() => window.__dispatchFromCpp({ type: "setMixerState", data: window.fixtureStrips }));
    assert.equal(Number(await layeredChair.locator('.channel-strip input[type="number"]').nth(1).inputValue()), 0);
    await layeredChair.locator('.channel-strip input[type="number"]').first().focus();
    await page.evaluate(() => { window.fixtureMessages = []; });
    await page.keyboard.press("Meta+z");
    assert.equal(await page.evaluate(() => window.fixtureMessages.some(x => x.type === "undo")), false,
        "text-field Undo must not undo the project");
    await viewButton.focus();
    await page.keyboard.press("Meta+Shift+z");
    assert.equal((await page.evaluate(() => window.fixtureMessages.at(-1))).type, "redo");
    await viewButton.click();
    await page.getByRole("button", { name: "Chairs…", exact: true }).click();
    const manager = page.getByRole("dialog", { name: "Chair Manager", exact: true });
    await page.evaluate(() => {
        window.__dispatchFromCpp({ type: "setChairState", data: window.fixtureChairs });
        window.__dispatchFromCpp({ type: "setUndoState", data: {
            canUndo: true, canRedo: false, undoDescription: "Edit chair 'New flute'", redoDescription: "",
        } });
    });
    const firstName = manager.getByRole("textbox", { name: "Name for Flute 1", exact: true });
    await firstName.fill("New flute");
    await firstName.press("Enter");
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
        { type: "updateChair", payload: [{ id: "chair-0", name: "New flute" }] });
    await page.evaluate(() => window.__dispatchFromCpp({ type: "setChairState",
        data: window.fixtureChairs.map(c => c.id === "chair-0" ? { ...c, name: "New flute" } : c) }));
    await manager.getByRole("button", { name: "Undo", exact: true }).click();
    assert.equal((await page.evaluate(() => window.fixtureMessages.at(-1))).type, "undo");
    await page.evaluate(() => window.__dispatchFromCpp({ type: "setChairState", data: window.fixtureChairs }));
    assert.equal(await firstName.inputValue(), "Flute 1");
    await manager.getByRole("combobox", { name: "Player type for Flute 1", exact: true }).selectOption("section");
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
        { type: "updateChair", payload: [{ id: "chair-0", name: "Flute 1", role: "section" }] });
    const firstRow = manager.locator(".chair-row").first();
    await firstRow.getByRole("button", { name: "Delete", exact: true }).click();
    await firstRow.getByRole("button", { name: "Confirm delete", exact: true }).click();
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
        { type: "deleteChair", payload: ["chair-0"] });
    assert.equal(await manager.getByRole("button", { name: "Redo", exact: true }).isDisabled(), true);
    await manager.getByRole("button", { name: "Close", exact: true }).click();
    // Audio diagnostics use isolated fixtures, never the live server/device.
    await page.evaluate(() => window.__dispatchFromCpp({ type: "setAudioDiagnostics", data: {
        running: true, ageMs: 0, load: 70, peakLoad: 88, sampleRate: 44100,
        blockSize: 512, buildConfiguration: "Release", returnFresh: true, returnSampleRate: 44100,
        returnProtocol: 2, requestedDelayMs: 1000, effectiveDelayMs: 1000,
        renderAhead: { enabled: true, idle: true, realtimeScheduling: true, queuedMs: 61.2, targetMs: 69.7,
            skippedFrames: 0, hostClockAgeMs: 2.1 },
        device: { name: "Fixture audio interface", type: "Test audio", sampleRate: 44100, bufferSize: 512 },
        plugins: Array.from({ length: 30 }, (_, i) => ({
            id: `slot-${i}`, owner: `Violin ${i + 1} / A long library name`, kind: "Instrument",
            plugin: "Vienna Synchron Player", averageMs: i / 10, peakMs: 3, maxMs: 5,
            ageMs: 100, blockSize: 512, sampleRate: 44100,
        })),
    } }));
    await page.getByRole("button", { name: "Audio performance", exact: true }).click();
    const performance = page.getByRole("dialog", { name: "Audio performance", exact: true });
    await performance.waitFor({ state: "visible" });
    assert.match(await performance.innerText(), /61.2 ms queued · 69.7 ms target/);
    assert.match(await performance.innerText(), /Render-ahead reserve · Idle/);
    assert.match(await performance.innerText(), /1000 ms effective \/ 1000 ms requested/);
    assert.doesNotMatch(await performance.innerText(), /Long callback gaps/);
    assert.equal(await performance.getByRole("slider").count(), 0,
        "Audio Performance must not contain settings controls");
    assert.equal(await performance.getByRole("combobox").count(), 0,
        "render-worker selection belongs in Settings");
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
    assert.equal(report.samples[0].renderAhead.targetMs, 69.7);
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "audio-performance.png") });
    await performance.locator("tbody tr").first().scrollIntoViewIfNeeded();
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "audio-plugin-timings.png") });
    await performance.getByRole("button", { name: "Close", exact: true }).click();
    await performance.waitFor({ state: "hidden" });
    await viewButton.click();
    await page.getByRole("button", { name: "Settings…", exact: true }).click();
    assert.deepEqual(await page.evaluate(() => window.fixtureMessages.at(-1)),
        { type: "showView", payload: ["settings"] });
    // A locked three-layer chair: cross the target, restore from a serialized
    // snapshot while siblings are silent, then lower the layer and recover the blend.
    // The fixture only applies bridge messages; all compensation runs in the UI.
    const installLockFixture = async (targetPage, snapshot) => targetPage.evaluate(snapshot => {
        window.lockFixture = snapshot;
        window.fixtureMessages = [];
        const publish = () => {
            window.__dispatchFromCpp({ type: "setChairState", data: [snapshot.chair] });
            window.__dispatchFromCpp({ type: "setProjectSettings", data: structuredClone(snapshot.settings) });
            window.__dispatchFromCpp({ type: "setMixerState", data: structuredClone(snapshot.strips) });
        };
        window.__JUCE__ = { backend: { emitEvent: (_event, request) => {
            const message = request.params[0];
            window.fixtureMessages.push(message);
            if (message.type !== "setMixerControls") return;
            const [changes, , levels = []] = message.payload;
            snapshot.strips = snapshot.strips.map(s => ({ ...s, ...changes.find(c => c.id === s.id) }));
            for (const level of levels) snapshot.settings.chairLevels[level.id] = level;
            queueMicrotask(publish);
        } } };
        publish();
    }, snapshot);
    const lockedSnapshot = {
        chair: { id: "locked", name: "Locked blend", family: "wind", role: "solo", port: 0, channel: 0, ordinal: 1, flatIndex: 0 },
        strips: [0.5, 0.3, 0.2].map((power, i) => ({
            id: `blend-${i}`, chairId: "locked", family: "wind", layerName: `Layer ${i + 1}`,
            library: "Blend", inputPort: 0, inputChannel: 0, gainDb: 10 * Math.log10(power),
            active: true, muted: false, soloed: false, pluginUid: 0,
        })),
        settings: { playbackDelayMs: 1000, lockedChairIds: ["locked"],
            chairLevels: { locked: { targetDb: 0, weights: { "blend-0": 0.5, "blend-1": 0.3, "blend-2": 0.2 } } } },
    };
    await installLockFixture(page, lockedSnapshot);
    const targetSlider = page.getByRole("slider", { name: "Target level for Locked blend", exact: true });
    const targetPosition = await targetSlider.inputValue();
    const layerGain = page.locator('.channel-strip input[type="number"]').first();
    await layerGain.fill("3");
    await layerGain.press("Enter");
    assert.equal(await targetSlider.inputValue(), targetPosition);
    assert.match(await page.locator(".combined-level-marker").getAttribute("aria-label"), /Combined gain \+3.0 dB; target \+0.0 dB/);
    const savedExcess = await page.evaluate(() => JSON.parse(JSON.stringify(window.lockFixture)));
    assert.deepEqual(savedExcess.strips.slice(1).map(s => s.gainDb), [-120, -120]);
    assert.equal(savedExcess.settings.chairLevels.locked.targetDb, 0);
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "locked-chair-excess.png") });
    await page.reload();
    await page.waitForSelector(".mixer-container");
    await installLockFixture(page, savedExcess);
    assert.equal(await targetSlider.inputValue(), targetPosition);
    await layerGain.fill("-3");
    await layerGain.press("Enter");
    assert.equal(await page.locator(".combined-level-marker").count(), 0);
    const recovered = await page.evaluate(() => window.lockFixture);
    const powers = recovered.strips.map(s => Math.pow(10, s.gainDb / 10));
    assert.ok(Math.abs(powers.reduce((a, b) => a + b, 0) - 1) < 1e-9);
    assert.ok(Math.abs(powers[1] / powers[2] - 1.5) < 1e-9);
    await targetSlider.focus();
    await page.keyboard.press("PageUp");
    assert.ok((await page.evaluate(() => window.lockFixture.settings.chairLevels.locked.targetDb)) > 0);
    // Authoritative Undo state replaces the keyboard-edited target and shadow gains.
    await installLockFixture(page, recovered);
    assert.equal(await targetSlider.inputValue(), targetPosition);
    assert.equal(await page.locator(".combined-level-marker").count(), 0);

    // The standalone Debug window must render from index.html and honor both
    // its initial tab and later native focus requests.
    const debugPage = await context.newPage();
    debugPage.setDefaultTimeout(10000);
    debugPage.on("pageerror", error => errors.push(error.message));
    await debugPage.goto(`http://127.0.0.1:${server.address().port}/index.html?mode=debug&tab=plugins`);
    await debugPage.locator(".tab-nav").waitFor();
    assert.equal(await debugPage.getByRole("button", { name: "Plugins", exact: true }).evaluate(button => button.classList.contains("active")), true);
    await debugPage.evaluate(() => window.__dispatchFromCpp({ type: "showDebugTab", data: "capture" }));
    assert.equal(await debugPage.getByRole("button", { name: "MIDI Capture", exact: true }).evaluate(button => button.classList.contains("active")), true);
    assert.equal(await debugPage.getByRole("button", { name: "Plugins", exact: true }).evaluate(button => button.classList.contains("active")), false);
    assert.equal(await debugPage.locator(".tab-nav button.active").count(), 1);
    if (process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR)
        await debugPage.screenshot({ path: path.join(process.env.FIDDLE_LAYOUT_SCREENSHOT_DIR, "debug-midi-capture.png") });
    await debugPage.close();

    // Use the real Library Manager in its own window mode. Native integration
    // tests execute the command; this fixture checks dispatch and status rendering.
    const libraryPage = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
    libraryPage.setDefaultTimeout(10000);
    libraryPage.on("pageerror", error => errors.push(error.message));
    await libraryPage.goto(`http://127.0.0.1:${server.address().port}/?view=library`);
    await libraryPage.waitForSelector(".lm-root");
    await libraryPage.evaluate(() => {
        window.fixtureMessages = [];
        window.__JUCE__ = { backend: { emitEvent: (_event, request) => {
            window.fixtureMessages.push(request.params[0]);
        } } };
        window.__dispatchFromCpp({ type: "setLibraryData", data: {
            id: "test-library", name: "Test library", patches: [
                { id: "used", name: "Linked patch", usageCount: 2, outOfDateLayerCount: 2 },
                { id: "unused", name: "Unused patch", usageCount: 0, outOfDateLayerCount: 0 },
            ],
        } });
    });
    const libraryEditor = libraryPage.getByRole("dialog", { name: "VST LIBRARY EDITOR" });
    const linked = libraryEditor.locator(".inst-row").first();
    const update = linked.getByRole("button", { name: "Update Layers", exact: true });
    assert.equal(await update.isEnabled(), true);
    assert.equal(await libraryEditor.locator(".inst-row").nth(1)
        .getByRole("button", { name: "Update Layers", exact: true }).isDisabled(), true);
    await update.click();
    assert.deepEqual(await libraryPage.evaluate(() => window.fixtureMessages.filter(
        x => x.type === "updateLayersFromLibraryPatch")),
        [{ type: "updateLayersFromLibraryPatch", payload: ["used"] }]);
    await libraryPage.evaluate(() => {
        window.__dispatchFromCpp({ type: "setLibraryLayerStatus", data: [
            { patchId: "used", usageCount: 2, outOfDateLayerCount: 0 },
        ] });
        window.__dispatchFromCpp({ type: "libraryLayersUpdateResult", data: {
            success: true, patchId: "used", message: "OK: Updated 2 layers. Undo in the main mixer.",
        } });
    });
    await update.waitFor({ state: "visible" });
    assert.equal(await update.isDisabled(), true);
    assert.match(await libraryEditor.getByRole("status").innerText(), /Undo in the main mixer/);
    // Simulate main-project Undo: the library's counts must become stale again.
    await libraryPage.evaluate(() => window.__dispatchFromCpp({ type: "setLibraryLayerStatus", data: [
        { patchId: "used", usageCount: 2, outOfDateLayerCount: 2 },
    ] }));
    await libraryPage.waitForFunction(() => !document.querySelector(".action-update-layers").disabled);
    await linked.getByRole("textbox", { name: "Patch name", exact: true }).fill("Unsaved draft");
    await libraryPage.evaluate(() => window.__dispatchFromCpp({ type: "setLibraryLayerStatus", data: [
        { patchId: "used", usageCount: 3, outOfDateLayerCount: 1 },
    ] }));
    assert.equal(await linked.getByRole("textbox", { name: "Patch name", exact: true }).inputValue(), "Unsaved draft");
    assert.equal(await update.isDisabled(), true, "status refresh must preserve the draft/dirty guard");

    // Draft history is local, including a text commit, delete/restore, batching,
    // player identity, save checkpoints and rejected saves.
    const undoDraft = libraryEditor.getByRole("button", { name: "Undo draft", exact: true });
    const redoDraft = libraryEditor.getByRole("button", { name: "Redo draft", exact: true });
    await linked.getByRole("textbox", { name: "Patch name", exact: true }).blur();
    await libraryPage.keyboard.press("Meta+z");
    assert.equal(await linked.getByRole("textbox", { name: "Patch name", exact: true }).inputValue(), "Linked patch");
    assert.equal(await update.isEnabled(), true);
    await libraryPage.keyboard.press("Meta+Shift+z");
    assert.equal(await linked.getByRole("textbox", { name: "Patch name", exact: true }).inputValue(), "Unsaved draft");
    await linked.getByRole("button", { name: "Delete", exact: true }).click();
    assert.equal(await libraryEditor.locator(".inst-row").count(), 1);
    await undoDraft.click();
    assert.equal(await libraryEditor.locator(".inst-row").count(), 2);
    await linked.getByRole("button", { name: "Duplicate", exact: true }).click();
    assert.equal(await libraryEditor.locator(".inst-row").count(), 3);
    await undoDraft.click();
    assert.equal(await libraryEditor.locator(".inst-row").count(), 2);
    await libraryPage.evaluate(() => window.__dispatchFromCpp({ type: "setPluginList", data: [
        { uid: 101, name: "Player A" }, { uid: 102, name: "Player B" },
    ] }));
    await linked.locator(".ir-vst select").selectOption("101");
    await linked.getByRole("button", { name: "⚙️" }).click();
    const firstPreview = await libraryPage.evaluate(() => window.fixtureMessages.findLast(x => x.type === "openLibraryPatchEditor").payload[0].previewId);
    await linked.locator(".ir-vst select").selectOption("102");
    await undoDraft.click();
    assert.equal(await linked.locator(".ir-vst select").inputValue(), "101");
    await linked.getByRole("button", { name: "⚙️" }).click();
    assert.equal(await libraryPage.evaluate(() => window.fixtureMessages.findLast(x => x.type === "openLibraryPatchEditor").payload[0].previewId), firstPreview);
    await libraryPage.evaluate(id => window.__dispatchFromCpp({ type: "libraryPatchPreviewChanged", data: id }), firstPreview);
    await libraryEditor.getByRole("button", { name: "Save", exact: true }).click();
    assert.equal(await libraryEditor.getByRole("button", { name: "Save", exact: true }).isDisabled(), true);
    await libraryPage.evaluate(() => window.__dispatchFromCpp({ type: "librarySaveResult", data: {
        id: "test-library", success: false, message: "Test save failure: draft kept",
    } }));
    assert.equal(await libraryEditor.isVisible(), true);
    await libraryEditor.getByRole("button", { name: "Save", exact: true }).waitFor();
    assert.equal(await libraryEditor.getByRole("button", { name: "Save", exact: true }).isEnabled(), true);
    await libraryEditor.getByRole("button", { name: "Save", exact: true }).click();
    await libraryPage.evaluate(() => window.__dispatchFromCpp({ type: "librarySaveResult", data: {
        id: "test-library", success: true,
    } }));
    await undoDraft.click();
    assert.equal(await libraryEditor.getByRole("button", { name: "Save", exact: true }).isEnabled(), true);
    await redoDraft.click();
    assert.equal(await libraryEditor.getByRole("button", { name: "Save", exact: true }).isDisabled(), true);
    await libraryPage.setViewportSize({ width: 1100, height: 700 });
    await libraryEditor.locator(".inst-content").evaluate(node => { node.scrollLeft = node.scrollWidth; });
    const actionBounds = await linked.getByRole("button", { name: "Delete", exact: true }).boundingBox();
    const panelBounds = await libraryEditor.boundingBox();
    assert.ok(actionBounds.x + actionBounds.width <= panelBounds.x + panelBounds.width,
        "right-hand patch actions must be reachable at the default window size");
    if (process.env.FIDDLE_LIBRARY_SCREENSHOT)
        await libraryPage.screenshot({ path: process.env.FIDDLE_LIBRARY_SCREENSHOT });
    await libraryPage.setViewportSize({ width: 1440, height: 1000 });
    await libraryEditor.locator(".inst-content").evaluate(node => { node.scrollLeft = 0; });
    // One quick-add ensemble is one undo step, irrespective of its row count.
    await libraryEditor.locator(".ens-card").first().click();
    assert.ok(await libraryEditor.locator(".inst-row").count() > 2);
    await undoDraft.click();
    assert.equal(await libraryEditor.locator(".inst-row").count(), 2);
    await libraryEditor.getByRole("button", { name: "+ Add Patch", exact: true }).click();
    await libraryEditor.getByRole("button", { name: "Close library editor", exact: true }).click();
    await libraryEditor.getByRole("button", { name: "Keep editing", exact: true }).click();
    assert.equal(await libraryEditor.locator(".inst-row").count(), 3);
    await libraryEditor.getByRole("button", { name: "Close library editor", exact: true }).click();
    await libraryPage.keyboard.press("Escape");
    assert.equal(await libraryEditor.isVisible(), true, "Escape must not accept the discard confirmation");
    await libraryEditor.getByRole("button", { name: "Discard changes", exact: true }).click();
    await libraryPage.evaluate(() => window.__dispatchFromCpp({ type: "setLibraryCatalogHistory", data: {
        canUndo: true, canRedo: false, undoDescription: "Save library: Test library",
    } }));
    await libraryPage.getByRole("button", { name: "Undo catalog", exact: true }).click();
    const libraryCommands = await libraryPage.evaluate(() => window.fixtureMessages);
    assert.ok(libraryCommands.some(x => x.type === "undoLibraryCatalog"));
    assert.equal(libraryCommands.some(x => x.type === "undo" || x.type === "redo"), false,
        "library operations must never dispatch project history");
    await libraryPage.close();
    assert.deepEqual(errors, [], "no browser runtime errors");
    console.log("PASS: chair layout, mixer Undo, project settings, audio diagnostics, library update/status, and separate library history");
} finally {
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
}
