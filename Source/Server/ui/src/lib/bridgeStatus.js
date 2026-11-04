// Live bridge-map status for a chair, from the server's setBridgeMapState.
// Bridge assignments use 0-based ports and 1-based channels; chairs use
// 0-based ports and 0-based channels.

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

export function verificationText(a) {
    if (a.beacon === "mismatched") return `Dorico is playing a different revision (beacon ${a.receivedBeacon.join("/")}, expected ${a.expectedBeacon.join("/")}) · notes withheld`;
    if (a.beacon === "matched") return "Beacon matches";
    if (a.verified) return "Export matches · confirmed by user";
    if (a.beacon === "absent") return "Waiting for the revision beacon · start playback";
    return "Unverified · notes withheld";
}

export function counterText(a) {
    const parts = [];
    if (a.withheldNotes) parts.push(`${a.withheldNotes} notes withheld`);
    if (a.invalidCommands) parts.push(`${a.invalidCommands} invalid commands`);
    if (a.ignoredOffs) parts.push(`${a.ignoredOffs} stale Offs ignored`);
    if (a.debouncedOffs) parts.push(`${a.debouncedOffs} early Offs suppressed`);
    return parts.join(" · ");
}
