import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const mixerSource = readFileSync(
    new URL("../src/lib/MixerPanel.svelte", import.meta.url),
    "utf8",
);

test("the main toolbar exposes one directory for workspaces, setup and diagnostics", () => {
    assert.match(mixerSource, />\s*Library Manager\s*<\/button>/);
    assert.match(mixerSource, /dispatchCpp\("showView", "library-manager"\)/);
    assert.match(mixerSource, />\s*Project History\s*<\/button>/);
    assert.match(mixerSource, />\s*Audio Performance\s*<\/button>/);
    assert.match(mixerSource, />\s*MIDI Capture\s*<\/button>/);
    assert.match(mixerSource, />\s*Settings…\s*<\/button>/);
    assert.match(mixerSource, /dispatchCpp\("showView", "settings"\)/);
    assert.match(mixerSource, /dispatchCpp\("showView", "print"\)/);
    assert.match(mixerSource, />Print Audio…<\/button>/);
    assert.doesNotMatch(mixerSource, /<ToolbarMenu label="Setup">/);
});

test("native View menu separates windows from mixer layout and tools share an isolated host", () => {
    const native = readFileSync(new URL("../../Main.cpp", import.meta.url), "utf8");
    const component = readFileSync(new URL("../../MainComponent.cpp", import.meta.url), "utf8");
    const window = readFileSync(new URL("../../ToolWindow.h", import.meta.url), "utf8");
    const entry = readFileSync(new URL("../src/main.js", import.meta.url), "utf8");
    assert.match(native, /windows.addItem\(kPrintMenuItemId, "Print Audio\.\.\."\)/);
    assert.match(native, /case kPrintMenuItemId: return "print"/);
    assert.match(component, /viewId == "print"/);
    assert.match(component, /make_unique<ToolWindow>/);
    assert.match(entry, /\['print', 'chairs', 'bus-manager', 'audio-performance'\].includes\(toolId\) \? ToolPane : App/);
    assert.match(window, /index.html\?mode=/);
    assert.match(native, /addSubMenu\("Windows", windows\)/);
    assert.match(native, /addSubMenu\("Mixer layout", layout\)/);
    assert.match(mixerSource, />Windows<\/span>/);
    assert.match(mixerSource, />Mixer layout<\/span>/);
    assert.match(component, /loadWindowSettings\("tool-" \+ viewId\)/);
    assert.match(window, /pending_ \|\| !isVisible\(\) \|\| isMinimised\(\)/);
    assert.match(window, /dirty_\[type\] = data/);
    assert.match(window, /SafePointer<ToolWindow>/);
    assert.doesNotMatch(mixerSource, /<ChairManager|<BusManager/);
    assert.doesNotMatch(window, /stopMixPrint/);
});
