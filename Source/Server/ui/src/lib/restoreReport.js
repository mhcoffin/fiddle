// What the main window shows when a Dorico project connects: the server's
// restore report (ProjectRestoreReport.h) plus the layers that can't play as
// saved, read from the mixer state as plug-ins finish loading.

const NAMES_SHOWN = 4;

function layerLabel(strip) {
    const name = strip.layerName || strip.id || "Layer";
    return strip.library ? `${name} (${strip.library})` : name;
}

function listed(labels) {
    if (labels.length <= NAMES_SHOWN) return labels.join(", ");
    return `${labels.slice(0, NAMES_SHOWN).join(", ")} and ${labels.length - NAMES_SHOWN} more`;
}

/** Problems that make the project sound different from when it was saved. */
export function restoreProblems(strips) {
    const problems = [];
    const list = Array.isArray(strips) ? strips : [];
    const missing = list.filter(s => s.pluginStatus === "missing").map(layerLabel);
    if (missing.length)
        problems.push(`Plug-in not installed, so ${missing.length === 1 ? "this layer is" : "these layers are"} silent: ${listed(missing)}.`);
    const failed = list.filter(s => s.pluginStatus === "failed").map(layerLabel);
    if (failed.length)
        problems.push(`Plug-in failed to load: ${listed(failed)}.`);
    const scripts = list.flatMap(s => (s.luaPlugins || []).filter(p => !p.loaded)
        .map(p => `${p.name || "script"} on ${s.layerName || "a layer"}`));
    if (scripts.length)
        problems.push(`Fiddle script not found, so it isn't applied: ${listed(scripts)}.`);
    return problems;
}

/**
 * Whether to show the banner: always for warnings and for the user's own
 * choices; a plain successful restore only when something can't play as saved,
 * something it depends on changed since it was saved, or unsaved edits were
 * saved first.
 */
export function showRestoreReport(report, problems) {
    if (!report || !report.kind) return false;
    // Edits saved before switching are always worth saying.
    if (report.kind === "exact")
        return problems.length > 0 || report.savedEdits === true || (report.changes?.length ?? 0) > 0;
    return true;
}
