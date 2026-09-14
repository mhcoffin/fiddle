export function audioCpuLabel(data, receivedAgeMs = 0) {
    if (!data || receivedAgeMs > 3000) return "Audio CPU —";
    if (!data.running) return "Audio stopped";
    if (data.ageMs < 0 || data.ageMs + receivedAgeMs > 2000) return "Audio CPU stale";
    return `Audio CPU ${Math.round(data.load)}%`;
}

export function appendDiagnosticSample(history, data, now) {
    if (history.length && now - history[history.length - 1].timeMs < 1000) return history;
    return [...history.slice(-119), { timeMs: now, time: new Date(now).toISOString(), ...data }];
}

export function diagnosticReport(history, marks) {
    return JSON.stringify({
        description: "Fiddle audio diagnostics. Load is render wall time / audio block duration, not whole-machine CPU. Render-ahead uses the existing playback delay; queued/target milliseconds show the completed-audio reserve. Protocol 2 advances through safety-muted recovery rather than replaying late audio; the mute remains latched until the reserve is effectively full and then fades in. Plugin timings include waiting inside plugins. Per-plugin windows are independent: do not sum their peaks. Counts are cumulative; native counts may reset on plugin recreation. Sample times are UI receipt times, not exact glitch times.",
        marks, samples: history,
    }, null, 2);
}

export function pluginTimingRows(data) {
    return (Array.isArray(data?.plugins) ? data.plugins : [])
        .map(row => ({ ...row, fresh: !row.bypassed && data.running && row.ageMs >= 0 && row.ageMs < 2000 }))
        .sort((a, b) => Number(b.fresh) - Number(a.fresh) || b.averageMs - a.averageMs || String(a.owner).localeCompare(String(b.owner)));
}
