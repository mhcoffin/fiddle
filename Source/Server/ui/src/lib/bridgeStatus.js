// Live bridge-map status for a chair, from the server's setBridgeMapState.
// Bridge assignments use 0-based ports and 1-based channels; chairs use
// 0-based ports and 0-based channels.
//
// The wording is for musicians: "version ID" is the map's revision beacon
// (keyswitches 126/127 in its Init), and "not played" is a withheld note.

export function bridgeForChair(state, chair) {
    const assignment = (state?.assignments || []).find(a => a.port === chair.port && a.channel === chair.channel + 1);
    if (!assignment) return null;
    const section = (state.sections || []).find(s => s.hash === assignment.hash);
    const doc = (state.documents || []).find(d => d.hash === assignment.hash);
    return { ...assignment, label: section?.label || doc?.name || "Bridge map" };
}

export function isPlaying(a) {
    return a.beacon !== "mismatched" && (a.verified || a.beacon === "matched");
}

/** "Orchestral strings map", without doubling a trailing "map". */
export const mapName = (a) => /\bmap$/i.test(a.label || "") ? a.label : `${a.label} map`;

export function verificationText(a) {
    if (a.beacon === "mismatched") return "Dorico has a different version · its notes are muted";
    if (a.beacon === "matched") return "✓ Dorico has this version";
    if (a.verified) return "✓ Confirmed from an export";
    if (a.beacon === "absent") return "Play to check the version";
    return "Not checked · its notes are muted";
}

/** Why a note wasn't played, by the server's reason key. */
export const WITHHELD_REASONS = Object.freeze({
    unverified: "map not checked",
    beaconMismatch: "Dorico has a different version",
    baseUnknown: "arrived before the starting articulation",
    invalidCommand: "unknown articulation command",
    outsideRange: "outside the instrument's range",
    unmappedPitch: "pitch not in the percussion map",
});
const reasonText = (key) => WITHHELD_REASONS[key] || "unknown reason";
const plural = (count, word) => `${count} ${word}${count === 1 ? "" : "s"}`;

function withheldCounts(a) {
    const byReason = Object.entries(a.withheldByReason || {}).filter(([, count]) => count > 0);
    if (byReason.length) return byReason.sort((x, y) => y[1] - x[1]);
    return a.withheldNotes && a.lastWithheldReason ? [[a.lastWithheldReason, a.withheldNotes]] : [];
}

/**
 * " · 2 notes not played (outside the instrument's range)", or "" when every
 * note played. A reason the status already gives (a different or unchecked
 * map) isn't repeated; several reasons are listed in the tooltip.
 */
export function withheldText(a) {
    if (!a.withheldNotes) return "";
    const reasons = withheldCounts(a);
    const statusSaysWhy = !isPlaying(a) && reasons.every(([key]) => key === "beaconMismatch" || key === "unverified");
    const why = reasons.length === 1 && !statusSaysWhy ? ` (${reasonText(reasons[0][0])})` : "";
    return ` · ${plural(a.withheldNotes, "note")} not played${why}`;
}

function explanation(a) {
    if (a.beacon === "mismatched")
        return `Dorico sent version ID ${a.receivedBeacon.join("/")}, but Fiddle installed ${a.expectedBeacon.join("/")}, `
            + "so Dorico is playing an older or edited copy of this map and Fiddle mutes its notes. "
            + "With Dorico closed, choose Install Playback Template, then reapply the Fiddle template in open projects.";
    if (a.beacon === "matched")
        return "When playback started, Dorico sent this map's version ID (keyswitches 126/127), "
            + "and it matches the version Fiddle installed.";
    if (a.verified)
        return "You confirmed that the map Dorico exported is the one Fiddle installed. "
            + "Playback also checks Dorico's version ID.";
    if (a.beacon === "absent")
        return "When playback starts, Dorico sends this map's version ID (keyswitches 126/127), "
            + "and Fiddle checks it matches the version it installed. Stopping clears the check.";
    return "This map sends no version ID, so Fiddle can't check it during playback and mutes its notes. "
        + "Export the map from Dorico and choose Verify assigned map under Bridge maps.";
}

/** Diagnostic counts that aren't withheld notes. */
export function counterText(a) {
    const parts = [];
    if (a.invalidCommands) parts.push(`${a.invalidCommands} invalid commands`);
    if (a.ignoredOffs) parts.push(`${a.ignoredOffs} stale Offs ignored`);
    if (a.debouncedOffs) parts.push(`${a.debouncedOffs} early Offs suppressed`);
    return parts.join(" · ");
}

/** The status tooltip: what the check means, why notes weren't played, diagnostics. */
export function statusTitle(a) {
    const reasons = withheldCounts(a);
    const notPlayed = reasons.length
        ? `Notes not played since the maps loaded: ${reasons.map(([key, count]) => `${count} ${reasonText(key)}`).join("; ")}.`
        : "";
    const counters = counterText(a);
    return [explanation(a), notPlayed, counters ? `Diagnostics: ${counters}.` : ""].filter(Boolean).join("\n\n");
}
