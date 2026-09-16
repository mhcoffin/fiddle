import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const source = readFileSync(new URL("../../MainComponent.cpp", import.meta.url), "utf8");

test("instrument playback suppression precedes gated parameter scanning", () => {
    const body = source.match(/void MainComponent::processPluginChangeNotifications\([\s\S]*?\n\}/)?.[0];
    assert.ok(body);
    assert.match(body, /if \(suppressPlaybackChanges && !explicitEdit\)\s+continue;/);
    assert.ok(body.indexOf("if (suppressPlaybackChanges && !explicitEdit)") < body.indexOf("observeStripPluginFingerprint(*strip)"));
    assert.ok(body.indexOf("observeStripPluginFingerprint(*strip)") < body.indexOf("strip->refreshPluginStateCache()"));
});

test("post-playback baseline includes instruments, strip inserts, master and buses", () => {
    const resume = source.match(/if \(pluginChangesWereSuppressed_\) \{([\s\S]*?)\n  \}/)?.[1];
    assert.match(resume, /captureStripPluginFingerprints\(\)/);
    assert.match(resume, /masterAudio\(\).captureParameterFingerprints\(\)/);
    assert.match(resume, /getAllGroupBuses\(\)/);
    assert.match(resume, /bus->audioEngine\(\).captureParameterFingerprints\(\)/);
    assert.doesNotMatch(resume, /getAllStrips\(\)/); // no duplicate strip scan
    const capture = source.match(/void MainComponent::captureStripPluginFingerprints\(\) \{([\s\S]*?)\n\}/)?.[1];
    assert.match(capture, /strip->pluginParameterFingerprint\(\)/);
    assert.match(capture, /strip->audioEngine\(\).captureParameterFingerprints\(\)/);
});
