// The playback template's status, from the server's setTemplateDirty:
// whether Dorico's installed template matches the chairs, and which chairs are
// new or changed since it was installed (PlaybackTemplateRecord.h).

const EMPTY = Object.freeze({ known: false, dirty: true, newChairIds: [], changedChairIds: [], removedCount: 0 });

/** Accepts the status object, or a bare boolean from older senders. */
export function parseTemplateStatus(data) {
    if (data && typeof data === "object") return { ...EMPTY, ...data };
    return { ...EMPTY, known: !data, dirty: !!data };
}

const count = (n, word) => `${n} ${word}${n === 1 ? "" : "s"}`;
const REINSTALL = "With Dorico closed, choose Install Playback Template, then reapply the Fiddle template in open projects.";

/** The banner above the chair list, or null when the template is current. */
export function templateBanner(status) {
    if (!status.dirty) return null;
    if (!status.known)
        return { title: "Fiddle has no record of the installed playback template",
            detail: `Install it once so Fiddle can tell when Dorico is out of date. ${REINSTALL}` };
    const parts = [];
    if (status.newChairIds.length) parts.push(count(status.newChairIds.length, "new chair"));
    if (status.changedChairIds.length) parts.push(`${count(status.changedChairIds.length, "chair")} changed`);
    if (status.removedCount) parts.push(`${count(status.removedCount, "chair")} removed`);
    return { title: `The playback template is out of date: ${parts.join(", ")}`, detail: REINSTALL };
}

/** A chair's note in its row: why it needs a reinstall, or "" when it's installed. */
export function chairTemplateNote(status, chairId) {
    if (!status.known) return "";
    if (status.newChairIds.includes(chairId)) return "Not in the installed template yet · reinstall";
    if (status.changedChairIds.includes(chairId)) return "Changed since the template was installed · reinstall";
    return "";
}
