import test from "node:test";
import assert from "node:assert/strict";
import { chairTemplateNote, parseTemplateStatus, templateBanner } from "../src/lib/templateStatus.js";

test("a current template shows no banner or notes", () => {
    const status = parseTemplateStatus({ known: true, dirty: false });
    assert.equal(templateBanner(status), null);
    assert.equal(chairTemplateNote(status, "fl"), "");
});

test("an out-of-date template names what changed, and each chair says why", () => {
    const status = parseTemplateStatus({ known: true, dirty: true, newChairIds: ["fl", "ob"], changedChairIds: ["cl"], removedCount: 1 });
    assert.equal(templateBanner(status).title, "The playback template is out of date: 2 new chairs, 1 chair changed, 1 chair removed");
    assert.match(templateBanner(status).detail, /With Dorico closed, choose Install Playback Template/);
    assert.equal(chairTemplateNote(status, "fl"), "Not in the installed template yet · reinstall");
    assert.equal(chairTemplateNote(status, "cl"), "Changed since the template was installed · reinstall");
    assert.equal(chairTemplateNote(status, "bn"), "");
});

test("without a record Fiddle says so and doesn't guess per chair", () => {
    const status = parseTemplateStatus({ known: false, dirty: true });
    assert.match(templateBanner(status).title, /no record/);
    assert.equal(chairTemplateNote(status, "fl"), "");
    // Older senders sent a bare boolean.
    assert.equal(parseTemplateStatus(false).dirty, false);
    assert.equal(templateBanner(parseTemplateStatus(true)).title, "Fiddle has no record of the installed playback template");
});
