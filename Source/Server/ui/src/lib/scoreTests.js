// Score tests pane (tests/score): display helpers for scoreTestState.

const instrumentNames = {
    "instrument.strings.violin": "Violin",
    "instrument.strings.viola": "Viola",
    "instrument.wind.flute": "Flute",
};

/** "instrument.strings.violin" → "Violin"; unknown IDs by their last part. */
export function instrumentLabel(id) {
    if (instrumentNames[id]) return instrumentNames[id];
    const last = String(id || "").split(".").pop() || "";
    return last ? last[0].toUpperCase() + last.slice(1) : "";
}

/** 19200 → "19 s"; 95000 → "1:35". */
export function formatLength(ms) {
    const seconds = Math.round((Number(ms) || 0) / 1000);
    return seconds < 60 ? `${seconds} s` : `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
}

/** The pane's state with defaults, whatever arrived. */
export function normalizeState(value) {
    const state = value && typeof value === "object" ? value : {};
    return {
        tests: Array.isArray(state.tests) ? state.tests : [],
        armed: !!state.armed,
        armedTest: state.armed_test || "",
        recordedNotes: Number(state.recorded_notes) || 0,
        discardedTakes: Number(state.discarded_takes) || 0,
        lastRun: state.last_run && typeof state.last_run === "object" ? state.last_run : null,
        error: state.error || "",
        folder: state.folder || "",
        runsFolder: state.runs_folder || "",
    };
}

/** One line for a finished run: "Pass: 18 of 18 notes" or the failure counts. */
export function runHeadline(run) {
    const s = run?.summary || {};
    const expected = Number(s.expected) || 0;
    if (run?.pass) return `Pass: ${s.passed} of ${expected} notes`;
    const parts = [];
    if (s.failed) parts.push(`${s.failed} failed`);
    if (s.missing) parts.push(`${s.missing} missing`);
    if (s.extra) parts.push(`${s.extra} extra`);
    if (!expected) parts.push("no notes expected");
    else if (!s.received) parts.push("no notes received");
    return `Fail: ${parts.join(", ") || "see the run record"} (${Number(s.passed) || 0} of ${expected} passed)`;
}

/** While armed: what has arrived so far. */
export function recordingStatus(state) {
    const notes = state.recordedNotes;
    const take = state.discardedTakes ? ` (latest playthrough; ${state.discardedTakes} earlier discarded)` : "";
    return notes ? `${notes} note${notes === 1 ? "" : "s"} received${take}` : "No notes received yet";
}

