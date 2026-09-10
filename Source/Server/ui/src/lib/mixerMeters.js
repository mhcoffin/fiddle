// Meter state intentionally lives outside strip/bus configuration. Replacing it
// must not rebuild chair groups, change a fader, or enumerate effect lists.
const level = (value) => Number.isFinite(value) ? value : -120;

export function parseMixerMeters(packet) {
    const bank = (values) => Object.fromEntries(
        Object.entries(values && typeof values === "object" ? values : {})
            .filter(([, pair]) => Array.isArray(pair) && pair.length === 2)
            .map(([id, pair]) => [id, [level(pair[0]), level(pair[1])]]),
    );
    return {
        strips: bank(packet?.strips),
        buses: bank(packet?.buses),
        masterPeakDb: level(packet?.masterPeakDb),
    };
}
