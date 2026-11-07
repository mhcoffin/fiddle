/**
 * Return strip IDs in the order the mixer renders them: each chair's layers,
 * then its chair strip (by chair bus ID) when the chair has layers and
 * `showsChairStrip(instrumentGroup)` says the strip is shown.
 */
export const flattenVisualStripOrder = (groups, showsChairStrip = () => true) =>
    groups.flatMap((group) =>
        group.instrGroups.flatMap((instrumentGroup) => [
            ...instrumentGroup.strips.map((strip) => strip.id),
            ...(instrumentGroup.strips.length && instrumentGroup.chairBusId
                && showsChairStrip(instrumentGroup)
                ? [instrumentGroup.chairBusId] : []),
        ]),
    );

/**
 * True when a chair bus has settings you can hear: a level other than 0 dB,
 * mute, solo, balance, or inserts. A one-layer chair shows its chair strip
 * only then, so nothing audible is hidden. Sends don't count; a one-layer
 * chair's layer strip shows them.
 */
export const chairHasSettings = (bus) => Boolean(bus && (
    Math.abs(bus.gainDb ?? 0) > 0.05
    || bus.muted || bus.soloed
    || Math.abs(bus.audio?.balance ?? 0) > 0.001
    || (bus.audio?.preFaderInserts?.length ?? 0) > 0
    || (bus.audio?.postFaderInserts?.length ?? 0) > 0));

/**
 * The chair bus IDs a selection stands for: selected chair strips, and the
 * chairs of selected layers (selecting a layer counts as selecting its chair).
 */
export const selectedChairBusIds = (selectedIds, strips, chairBusIds) => {
    const result = new Set();
    for (const id of selectedIds) {
        if (chairBusIds.has(id)) result.add(id);
        else {
            const strip = strips.find((item) => item.id === id);
            if (strip?.chairBusId) result.add(strip.chairBusId);
        }
    }
    return [...result];
};

/** Add the inclusive anchor-to-target range to an existing selection. */
export const addStripRange = (selectedIds, orderedIds, anchorId, targetId) => {
    const anchorIndex = orderedIds.indexOf(anchorId);
    const targetIndex = orderedIds.indexOf(targetId);
    if (anchorIndex < 0 || targetIndex < 0) return new Set(selectedIds);

    const first = Math.min(anchorIndex, targetIndex);
    const last = Math.max(anchorIndex, targetIndex);
    return new Set([...selectedIds, ...orderedIds.slice(first, last + 1)]);
};
