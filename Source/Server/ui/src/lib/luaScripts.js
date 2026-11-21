// What the Layer details panel says about a layer's Lua script and the file
// it came from. A layer runs the copy of the script saved with its version,
// so an edited or missing file doesn't change how it plays
// (docs/lua-scripts.md).

/**
 * The note under a script's row, or null when there's nothing to say.
 * canUseFile: offer "Use new file", which switches every layer using the
 * script to the file as it is now.
 */
export function luaFileNote(plugin) {
    if (!plugin?.loaded) return null;
    const fileName = plugin.fileName || "the file";
    if (plugin.fileStatus === "changed") {
        if (plugin.fileError)
            return {
                text: `${fileName} has changed, but it has an error, so this layer keeps playing its saved copy. ${plugin.fileError}`,
                canUseFile: false,
            };
        return {
            text: `${fileName} has changed since this copy was saved. This layer keeps playing the saved copy until you use the new file.`,
            canUseFile: true,
        };
    }
    if (plugin.fileStatus === "missing")
        return { text: `${fileName} isn't on this Mac. This layer plays the copy saved with the setup.`, canUseFile: false };
    return null;
}

export const useFileTitle = (plugin) =>
    `Every layer that uses ${plugin?.fileName || "this script"} switches to the file as it is now. You can undo this.`;

// ── Parameters (docs/lua-scripts.md, "Parameters") ──────────────────────
// A script declares them, and the mixer state gives each with its value:
// { name, label, type: "bool" | "int" | "float" | "enum", min, max, step,
//   unit, control: "knob" | "field", zeroLabel, randomize, options,
//   default, value }.

const decimalsOf = (step) => {
    const text = String(step);
    return text.includes("e-") ? Math.min(6, Number(text.split("e-")[1]))
        : text.includes(".") ? Math.min(6, text.split(".")[1].length) : 0;
};

// 1, 2 or 5 times a power of ten, at or above size.
const niceStep = (size) => {
    if (!(size > 0)) return 1;
    const power = 10 ** Math.floor(Math.log10(size));
    return [1, 2, 5, 10].map((m) => m * power).find((step) => step >= size * 0.999);
};

/** A number parameter's resolution and display precision. */
export function paramSteps(param) {
    if (param.type === "int") {
        const step = Math.max(1, Math.round(param.step || 1));
        return { step, fineStep: step, decimals: 0 };
    }
    const step = param.step > 0 ? param.step : niceStep((param.max - param.min) / 100);
    return { step, fineStep: step, decimals: decimalsOf(step) };
}

/** A value as its control shows it, without the unit. */
export function formatParamValue(param, value) {
    if (param.type === "bool") return value ? "On" : "Off";
    if (param.type === "enum") return String(value ?? "");
    const number = Number(value);
    if (!Number.isFinite(number)) return "";
    if (param.zeroLabel && number === 0) return param.zeroLabel;
    return param.type === "int" ? String(Math.round(number)) : number.toFixed(paramSteps(param).decimals);
}

/** Typed text as a number for the parameter, or null. */
export function parseParamText(param, text) {
    const clean = String(text ?? "").trim().replaceAll("−", "-");
    if (param.zeroLabel && clean.toLowerCase() === param.zeroLabel.toLowerCase()) return 0;
    const withoutUnit = param.unit && clean.toLowerCase().endsWith(param.unit.toLowerCase())
        ? clean.slice(0, -param.unit.length).trim() : clean;
    if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(withoutUnit)) return null;
    const number = Number(withoutUnit);
    return Number.isFinite(number) ? number : null;
}

/** Whether the user has changed a parameter from its default. */
export function isParamModified(param) {
    if (param.type === "int" || param.type === "float")
        return Math.abs(Number(param.value) - Number(param.default)) > 1e-9;
    return param.value !== param.default;
}

/** Whether any of a script's parameters differ from their defaults. */
export const hasModifiedParams = (plugin) => (plugin?.params ?? []).some(isParamModified);

/** A random whole value in the parameter's range, other than 0 and the current value. */
export function randomParamValue(param, random = Math.random) {
    const low = Math.max(Math.ceil(param.min), 1), high = Math.floor(param.max);
    if (high < low) return param.min;
    for (let tries = 0; tries < 8; tries++) {
        const value = low + Math.floor(random() * (high - low + 1));
        if (value !== param.value) return value;
    }
    return low;
}

/** What a script's row says about its parameters: declaration mistakes and dropped values. */
export const paramMessages = (plugin) => [...(plugin?.paramErrors ?? []), ...(plugin?.paramNotes ?? [])];

/**
 * What a strip's MIDI FX band shows: how many scripts, how many play (loaded
 * and not bypassed), whether one needs attention (didn't load, its file
 * changed or is missing, or its declaration has a mistake), and a label.
 */
export function midiFxSummary(strip) {
    const scripts = strip?.luaPlugins ?? [];
    const active = scripts.filter((s) => s.loaded && s.enabled !== false).length;
    const attention = scripts.some((s) => !s.loaded || s.fileStatus === "changed" ||
        s.fileStatus === "missing" || (s.paramErrors?.length ?? 0) > 0);
    const count = scripts.length;
    const label = count === 0 ? "none"
        : `${count} script${count === 1 ? "" : "s"}${active === count ? "" : active ? `, ${active} active`
            : scripts.every((s) => s.loaded && s.enabled === false) ? ", all bypassed" : ", none active"}`
            + (attention ? ", needs attention" : "");
    return { count, active, attention, label };
}
