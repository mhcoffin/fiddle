// Meter state intentionally lives outside strip/bus configuration. Replacing it
// must not rebuild chair groups, change a fader, or enumerate effect lists.
const level = (value) => Number.isFinite(value) ? value : -120;
export const formatLufs = (loudness, key) => loudness?.valid === true && !loudness.incomplete && Number.isFinite(loudness[key])
    ? loudness[key].toFixed(1) : "—";
export const stereoLevel = (values) => Array.isArray(values) && values.length === 6
    ? [...values.slice(0, 4).map(level), values[4] === true, values[5] === true]
    : [-120, -120, -120, -120, false, false];

export function parseMixerMeters(packet) {
    const stereoBank = (values) => Object.fromEntries(Object.entries(values && typeof values === "object" && !Array.isArray(values) ? values : {})
        .filter(([, value]) => Array.isArray(value) && value.length === 6)
        .map(([id, values]) => [id, stereoLevel(values)]));
    const bank = (values) => Object.fromEntries(
        Object.entries(values && typeof values === "object" ? values : {})
            .filter(([, pair]) => Array.isArray(pair) && pair.length === 2)
            .map(([id, pair]) => [id, [level(pair[0]), level(pair[1])]]),
    );
    return {
        strips: bank(packet?.strips),
        buses: bank(packet?.buses),
        masterPeakDb: level(packet?.masterPeakDb),
        stereoStrips: stereoBank(packet?.stereoStrips),
        stereoBuses: stereoBank(packet?.stereoBuses),
        master: stereoLevel(packet?.master),
        loudness: {
            valid: packet?.loudness?.valid === true,
            incomplete: packet?.loudness?.incomplete === true,
            seconds: Number.isFinite(packet?.loudness?.seconds) ? Math.max(0, packet.loudness.seconds) : 0,
            ...Object.fromEntries(["integrated", "shortTerm", "truePeakLeft", "truePeakRight"].map(key =>
                [key, packet?.loudness?.valid === true && Number.isFinite(packet.loudness[key]) ? packet.loudness[key] : null])),
        },
    };
}
