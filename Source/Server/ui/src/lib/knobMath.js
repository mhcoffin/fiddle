export const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

// Spend most of the travel on useful mixing levels; reserve a short bottom
// segment for very quiet signals and the existing -120 dB silence sentinel.
const gainScale = [[0, -120], [0.08, -60], [0.35, -24], [0.55, -12], [0.8, 0], [1, 12]];
function interpolate(value, from, to) {
    value = clamp(value, gainScale[0][from], gainScale.at(-1)[from]);
    for (let i = 1; i < gainScale.length; i++) {
        const a = gainScale[i - 1], b = gainScale[i];
        if (value <= b[from])
            return a[to] + (value - a[from]) / (b[from] - a[from]) * (b[to] - a[to]);
    }
    return gainScale.at(-1)[to];
}
export const gainToPosition = (db) => interpolate(db, 1, 0);
export const positionToGain = (position) => interpolate(position, 0, 1);
export const formatGain = (db) => db <= -120 ? "−∞" : `${db > 0 ? "+" : ""}${db.toFixed(1)}`;
export function parseGain(text) {
    const clean = text.trim().replaceAll("−", "-").replace(/\s*dB$/i, "");
    if (/^-(?:∞|inf(?:inity)?)$/i.test(clean)) return -120;
    if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(clean)) return null;
    const value = Number(clean);
    return Number.isFinite(value) ? value : null;
}

export function knobKeyValue(key, value, min, max, step, fineStep, fine) {
    const increment = fine ? fineStep : step;
    const delta = { ArrowUp: increment, ArrowRight: increment,
        ArrowDown: -increment, ArrowLeft: -increment, PageUp: step * 10, PageDown: -step * 10 }[key];
    if (key === "Home") return min;
    if (key === "End") return max;
    return delta === undefined ? null : clamp(Number((value + delta).toFixed(6)), min, max);
}
