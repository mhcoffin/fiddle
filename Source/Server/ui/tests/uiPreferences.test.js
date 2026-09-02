import assert from "node:assert/strict";
import test from "node:test";

import {
    DEFAULT_STRIP_SIZE,
    MAX_UI_ZOOM,
    MIN_UI_ZOOM,
    STRIP_SIZE_PRESETS,
    readStripSize,
    readUiZoom,
    writeStripSize,
    writeUiZoom,
} from "../src/lib/uiPreferences.js";

const memoryStorage = (initial = {}) => {
    const values = new Map(Object.entries(initial));
    return {
        getItem: (key) => values.get(key) ?? null,
        setItem: (key, value) => values.set(key, value),
    };
};

test("comfortable strips are the default and presets remain ordered by width", () => {
    assert.equal(readStripSize(memoryStorage()), DEFAULT_STRIP_SIZE);
    assert.equal(DEFAULT_STRIP_SIZE, "comfortable");
    assert.deepEqual(
        Object.values(STRIP_SIZE_PRESETS).map(({ width }) => width),
        [80, 120, 160],
    );
});

test("strip-size preferences persist and invalid values fall back safely", () => {
    const storage = memoryStorage();
    assert.equal(writeStripSize(storage, "large"), "large");
    assert.equal(readStripSize(storage), "large");

    assert.equal(writeStripSize(storage, "enormous"), DEFAULT_STRIP_SIZE);
    assert.equal(readStripSize(storage), DEFAULT_STRIP_SIZE);
});

test("zoom preferences are rounded, clamped, and persisted", () => {
    const storage = memoryStorage();
    assert.equal(writeUiZoom(storage, 1.26), 1.3);
    assert.equal(readUiZoom(storage), 1.3);

    assert.equal(writeUiZoom(storage, 99), MAX_UI_ZOOM);
    assert.equal(writeUiZoom(storage, -99), MIN_UI_ZOOM);
});

test("unavailable browser storage does not prevent preference use", () => {
    const blockedStorage = {
        getItem: () => {
            throw new Error("blocked");
        },
        setItem: () => {
            throw new Error("blocked");
        },
    };

    assert.equal(readStripSize(blockedStorage), DEFAULT_STRIP_SIZE);
    assert.equal(readUiZoom(blockedStorage), 1);
    assert.equal(writeStripSize(blockedStorage, "compact"), "compact");
    assert.equal(writeUiZoom(blockedStorage, 1.5), 1.5);
});
