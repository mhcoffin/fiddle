// Library percussion maps: Dorico DrumKitNoteMapDefinitions owned by Fiddle
// (docs/library-percussion-maps.md). Pure helpers for the Library Manager
// editor and the layer picker; C++ validates again before saving.

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

/** Dorico's note name for a MIDI pitch (middle C = C3, as in Dorico). */
export function pitchName(pitch) {
    if (!Number.isInteger(pitch) || pitch < 0 || pitch > 127) return "";
    return `${NOTE_NAMES[pitch % 12]}${Math.floor(pitch / 12) - 2}`;
}

/** Dorico unpitched instruments, by the conventions of instruments.xml. */
export function isUnpitchedInstrument(entityId) {
    const id = String(entityId || "");
    return id.includes(".unpitched.") || id.startsWith("instrument.percussion.other.");
}

/** Techniques Dorico offers on unpitched instruments, for suggestions. */
export const PERCUSSION_TECHNIQUES = [
    "pt.natural", "pt.staccato", "pt.staccatissimo", "pt.tenuto", "pt.accent", "pt.marcato",
    "pt.roll", "pt.tremolo", "pt.buzzRoll", "pt.pressRoll", "pt.flatRoll", "pt.circularRoll", "pt.swooshRoll",
    "pt.centre", "pt.edgeDrums", "pt.rim", "pt.edgeCymbals", "pt.rimShot", "pt.rimKnock", "pt.crossStick",
    "pt.sideStick", "pt.shell", "pt.bell", "pt.frame", "pt.skin", "pt.stickShot", "pt.stickClick", "pt.stickSnap",
    "pt.backstick", "pt.standClick", "pt.tap", "pt.click", "pt.ping", "pt.ding", "pt.foot", "pt.scrape",
    "pt.swish", "pt.slap", "pt.slapCongas", "pt.bassTone", "pt.shake", "pt.crush", "pt.sizzle", "pt.zing",
    "pt.closed", "pt.halfOpen", "pt.open2", "pt.pedal", "pt.choke", "pt.muted", "pt.deadStroke", "pt.muffled",
    "pt.damp", "pt.laissezVibrer", "pt.ghostNote", "pt.snareOn", "pt.snareOff", "pt.right", "pt.left",
    "pt.stickShaft", "pt.stickShoulder", "pt.sticks", "pt.beaterRods", "pt.beaterBrushes", "pt.soft",
];

/** "pt.rim+pt.right" → ["pt.right", "pt.rim"] (Dorico's canonical order). */
export function parseTechniques(text) {
    return [...new Set(String(text || "").split(/[+,\s]+/).map((t) => t.trim()).filter(Boolean))].sort();
}

export function formatTechniques(techniques) {
    return [...(techniques || [])].sort().join("+");
}

/** A stable, Dorico-shaped entity ID for a map Fiddle creates. */
export function newPercussionMapId(name, random = () => crypto.randomUUID()) {
    const slug = String(name || "map").toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "") || "map";
    return `drumkitmap.user.fiddle.${slug}_${random().replace(/-/g, "").slice(0, 8)}`;
}

export function blankEntry(instrument = "") {
    return { instrument, techniques: ["pt.natural"], pitch: 60, keyswitches: [], name: "Natural", variant: "" };
}

export function newPercussionMap(name, instrument, random) {
    return {
        entityId: newPercussionMapId(name, random),
        name: String(name || "").trim() || "New percussion map",
        version: 1,
        forSingleInstrument: true,
        entries: [blankEntry(instrument)],
    };
}

/** A copy with its own identity, so both can live in the library. */
export function duplicatePercussionMap(map, random) {
    const name = `${map.name} copy`;
    return { ...structuredClone(map), entityId: newPercussionMapId(name, random), name, version: 1 };
}

/** Mirrors PercussionMap::validate(); an empty list means valid. */
export function validatePercussionMap(map) {
    const problems = [];
    if (!String(map?.entityId || "").trim()) problems.push("Missing entity ID");
    if (!String(map?.name || "").trim()) problems.push("Missing name");
    const entries = map?.entries || [];
    if (entries.length === 0) problems.push("Add at least one entry");
    const seen = new Set();
    entries.forEach((entry, index) => {
        const label = `Entry ${index + 1}${entry.name ? ` (${entry.name})` : ""}`;
        if (!String(entry.instrument || "").startsWith("instrument.")) problems.push(`${label}: choose a Dorico instrument`);
        if (!Number.isInteger(entry.pitch) || entry.pitch < 0 || entry.pitch > 127) problems.push(`${label}: pitch must be 0-127`);
        if ((entry.keyswitches || []).some((k) => !Number.isInteger(k) || k < 0 || k > 127))
            problems.push(`${label}: keyswitch must be 0-127`);
        if (!(entry.techniques || []).length) problems.push(`${label}: needs at least one technique (Natural is pt.natural)`);
        const key = `${entry.instrument}|${entry.variant || ""}|${formatTechniques(entry.techniques)}`;
        if (seen.has(key)) problems.push(`${label}: duplicate techniques for this instrument`);
        seen.add(key);
    });
    return problems;
}

/** Keyswitch field text ("24, 26") → numbers; null when invalid. */
export function parseKeyswitches(text) {
    const tokens = String(text || "").split(/[\s,]+/).filter(Boolean);
    const keys = tokens.map(Number);
    return keys.every((k) => Number.isInteger(k) && k >= 0 && k <= 127) ? keys : null;
}

const canonicalInstrument = (id) => String(id || "").split(".alias.")[0];

/** Whether a map has entries a layer on this instrument would use. */
export function coversInstrument(map, instrument) {
    const instruments = map?.instruments || [...new Set((map?.map?.entries || map?.entries || []).map((e) => e.instrument))];
    if (!instrument) return false;
    if (instruments.some((id) => canonicalInstrument(id) === canonicalInstrument(instrument))) return true;
    return instruments.length === 1; // a single-instrument map plays on any chair
}

/** Picker order: maps for this instrument first, then by name. */
export function sortForInstrument(maps, instrument) {
    const exact = (map) => (map.instruments || []).some((id) => canonicalInstrument(id) === canonicalInstrument(instrument));
    return [...(maps || [])].sort((a, b) =>
        Number(exact(b)) - Number(exact(a)) || String(a.name).localeCompare(String(b.name)));
}

/** Import candidates the dialog selects by default. */
export function defaultImportSelection(candidates) {
    return new Set((candidates || [])
        .filter((c) => c.status === "new" || c.status === "newer")
        .map((c) => c.map.entityId));
}
