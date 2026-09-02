export const STRIP_SIZE_STORAGE_KEY = "fiddle.mixer.stripSize";
export const UI_ZOOM_STORAGE_KEY = "fiddle.ui.zoom";

export const STRIP_SIZE_PRESETS = Object.freeze({
    compact: Object.freeze({ label: "Compact", width: 80 }),
    comfortable: Object.freeze({ label: "Comfortable", width: 120 }),
    large: Object.freeze({ label: "Large", width: 160 }),
});

export const DEFAULT_STRIP_SIZE = "comfortable";
export const DEFAULT_UI_ZOOM = 1;
export const MIN_UI_ZOOM = 0.5;
export const MAX_UI_ZOOM = 3;
export const UI_ZOOM_STEP = 0.1;

export const normalizeStripSize = (value) =>
    Object.hasOwn(STRIP_SIZE_PRESETS, value) ? value : DEFAULT_STRIP_SIZE;

export const normalizeUiZoom = (value) => {
    if (value === null || value === undefined || value === "")
        return DEFAULT_UI_ZOOM;
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) return DEFAULT_UI_ZOOM;
    const clamped = Math.max(MIN_UI_ZOOM, Math.min(MAX_UI_ZOOM, numeric));
    return Math.round(clamped * 10) / 10;
};

const readPreference = (storage, key) => {
    try {
        return storage?.getItem(key);
    } catch {
        return null;
    }
};

const writePreference = (storage, key, value) => {
    try {
        storage?.setItem(key, String(value));
    } catch {
        // Preferences are optional; the UI remains usable if storage is blocked.
    }
};

export const readStripSize = (storage) =>
    normalizeStripSize(readPreference(storage, STRIP_SIZE_STORAGE_KEY));

export const writeStripSize = (storage, value) => {
    const normalized = normalizeStripSize(value);
    writePreference(storage, STRIP_SIZE_STORAGE_KEY, normalized);
    return normalized;
};

export const readUiZoom = (storage) =>
    normalizeUiZoom(readPreference(storage, UI_ZOOM_STORAGE_KEY));

export const writeUiZoom = (storage, value) => {
    const normalized = normalizeUiZoom(value);
    writePreference(storage, UI_ZOOM_STORAGE_KEY, normalized);
    return normalized;
};
