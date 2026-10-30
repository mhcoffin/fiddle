#!/usr/bin/env python3
"""Generate the isolated strings inspection map and review artifacts (stdlib).

No installation or runtime integration. --check is strictly read-only.
The repository's 298-ID inventory is the portable input; --dorico-catalog
additionally checks it against a locally installed Dorico catalog.
The CLI defaults to the current r5 in r5/; --revision 2-4 reproduce frozen files.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import profile as legacy_profile
from profile import ALIASES, REVISION

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BASE_SOURCE = ROOT / "resources/Fiddle_Universal.doricolib"
INVENTORY_SOURCE = ROOT / "docs/dorico-built-in-technique-groups.md"
MAP_FILE = f"Fiddle-Orchestral-Strings-r{REVISION}.doricolib"

# Wire schema 2 (runtime compiler schema 2): every add-on Off names the
# technique it ends (Off = On + TYPED_OFF), so a late Off from a replaced
# technique cannot clear its successor. Init also carries a revision beacon on
# two reserved keyswitch pitches, letting the decoder confirm which map Dorico
# is actually playing. Schema 1 artifacts (r1-r3 strings, r1 winds) are frozen.
TYPED_OFF = 64
BEACON_PITCHES = (126, 127)
BEACON_MODULUS = 127 * 127
MUSICAL_RANGE = (21, 114)  # notes; keyswitch conduits lie outside it
# Base-row dynamics policies (volumeType): Dorico puts the level on CC1 just
# after the note-on, or in the note-on's velocity.
VOLUME_TYPES = {"cc1": ("kCC", 1), "velocity": ("kNoteVelocity", 0)}
LENGTH_CONDITIONS = ("NoteLength == kVeryShort", "NoteLength == kShort", "NoteLength == kMedium",
                     "NoteLength == kLong", "NoteLength > kLong")

# Playback-option overrides that pin every note-duration category to 100%, so
# Dorico sends written lengths and Fiddle applies the duration percentages
# (the layer map's overrides, else its copy of the project's options). Dorico
# must not shorten same-pitch notes either: Fiddle ends each one before its
# same-pitch successor itself, after scaling. Switch commands keep Dorico's
# default lead, so the decoder always sees the same timing; Fiddle applies
# the project's lead to each layer's own switches.
DURATION_OPTIONS = tuple(f"timingOptions.{name}DurationPercent" for name in
                         ("note", "staccato", "staccatissimo", "legato", "tenuto", "marcato"))
PINNED_OPTIONS = tuple((option, "int: 100") for option in DURATION_OPTIONS) + (
    ("timingOptions.alwaysTruncateAbuttingNotesOfSamePitch", "bool: false"),
    ("timingOptions.numTicksBeforeForSwitches", "int: 10"),
    # Linear dynamics over a known range, no articulation deltas: Fiddle reads
    # each note's level back and applies the layer's curve (dynamics-curve/).
    ("dynamicsOptions.dynamicCurveParams", 'string: "0.500000,1.000000,1.000000"'),
    ("dynamicsOptions.minProfileLevelForPlayback", "int: -8"),
    ("dynamicsOptions.maxProfileLevelForPlayback", "int: 8"),
    ("dynamicsOptions.accentDynamicDelta", "double: 0"),
    ("dynamicsOptions.marcatoDynamicDelta", "double: 0"),
    ("dynamicsOptions.tenutoDynamicDelta", "double: 0"),
    ("dynamicsOptions.ghostNoteDynamicDelta", "double: 0"))


def written_durations(spec):
    return getattr(spec, "WRITTEN_DURATIONS", False)


def wire_schema(spec):
    return getattr(spec, "WIRE_SCHEMA", 1)


def beacon(entity_id, revision):
    """14-bit-ish identity carried as two velocities 1..127 (never 0)."""
    digest = hashlib.sha256(f"{entity_id}:{revision}".encode()).digest()
    value = int.from_bytes(digest[:4], "big") % BEACON_MODULUS
    return (1 + value // 127, 1 + value % 127)


def inventory():
    result = {}
    for line in INVENTORY_SOURCE.read_text().splitlines():
        if not line.startswith("| `pt."):
            continue
        fields = [field.strip() for field in line.strip("|").split("|")]
        pt = fields[0].strip("`")
        if pt in result:
            raise ValueError(f"Duplicate inventory ID: {pt}")
        result[pt] = dict(label=fields[1], kind=fields[2], family=fields[4])
    if len(result) != 298:
        raise ValueError("Review a changed built-in inventory before generating")
    return result


def conduit_pool(spec=legacy_profile):
    pool = [("CC", n) for n in range(103, 120)]
    pool += [("KS", n) for n in list(range(21)) + list(range(115, 128))]
    if wire_schema(spec) >= 2:
        pool = [pair for pair in pool if pair not in {("KS", n) for n in BEACON_PITCHES}]
    return pool


def dimensions(spec=legacy_profile):
    pool = conduit_pool(spec)
    if len(spec.DIMENSIONS) > len(pool):
        raise ValueError(f"Profile exceeds the {len(pool) + 1}-conduit budget including the base")
    assignments = getattr(spec, "CONDUITS", None)
    if assignments is not None and set(assignments) != {d[0] for d in spec.DIMENSIONS}:
        raise ValueError("Explicit allocation must cover exactly the profile's dimensions")
    result = []
    for (key, members, natural, rationale), (kind, number) in zip(spec.DIMENSIONS, pool):
        if assignments is not None:
            kind, number = assignments[key]
            if (kind, number) not in pool:
                raise ValueError(f"Conduit outside reserved pool: {key}")
        reset = 0 if kind == "CC" else 1
        values = {"pt." + pt: reset + i + 1 for i, pt in enumerate(members.split())}
        overrides = getattr(spec, "VALUES", {}).get(key)
        if overrides is not None:
            if set(overrides) != set(values):
                raise ValueError(f"Explicit values must cover exactly the dimension's PTs: {key}")
            values = dict(overrides)
        result.append(dict(id=key, kind=kind, number=number, unspecified=reset,
                           natural_cancels=natural, rationale=rationale,
                           values=values))
    return result


def percussion_maps(spec):
    """The spec's percussion maps (unpitched sections), each a namespace with
    ENTITY_ID, NAME, INSTRUMENTS, CODES [(pitch, "pt.a[+pt.b]"[, keyswitch])]
    and FOR_SINGLE_INSTRUMENT. One map per section in practice; the probe
    carries two."""
    maps = getattr(spec, "PERCUSSION", None)
    return [] if maps is None else list(maps) if isinstance(maps, (list, tuple)) else [maps]


def pitch_codes(spec):
    """Every technique ID that some percussion-map code carries."""
    return {pt for m in percussion_maps(spec) for code in m.CODES for pt in code[1].split("+")}


def disposition(pt, row, owners, spec=legacy_profile):
    codes = pitch_codes(spec)
    if pt in codes:
        kinds = ["Pitch code"] + (["Base"] if pt in spec.BASE_IDS else []) + (["Add-on"] if pt in owners else [])
        return " + ".join(kinds), "Percussion map pitch" + ("; CC102 base" if pt in spec.BASE_IDS else "") \
            + (f"; add-on {owners[pt]}" if pt in owners else "")
    if pt in spec.BASE_IDS:
        return "Base", "CC102; original articulation/length encoding"
    if pt in owners:
        return "Add-on", owners[pt]
    if pt in spec.ALIASES:
        return "Alias", "Bowed aliases pt.natural in the built-in catalog; no distinct wire value"
    if pt in spec.EXCLUSIONS:
        return "Out of scope", spec.EXCLUSIONS[pt]
    family = row["family"]
    reason = spec.FAMILY_EXCLUSIONS.get(family, spec.FAMILY_EXCLUSIONS.get(family[:1]))
    if not reason:
        raise ValueError(f"Unaccounted PT {pt} ({family}); explicitly review its scope")
    return "Out of scope", reason


def validate(dims, catalog, spec=legacy_profile):
    owners = {}
    conduits = {("CC", 102)}
    names = set()
    for d in dims:
        if d["id"] in names or (d["kind"], d["number"]) in conduits:
            raise ValueError("Duplicate dimension or conduit")
        names.add(d["id"])
        conduits.add((d["kind"], d["number"]))
        if not d["values"]:
            raise ValueError("Empty dimension")
        if len(set(d["values"].values())) != len(d["values"]):
            raise ValueError("Different PTs in one dimension cannot share a wire value")
        for pt, value in d["values"].items():
            if pt not in catalog or pt in owners or pt in spec.BASE_IDS or pt in spec.ALIASES:
                raise ValueError(f"Invalid or multiply owned PT: {pt}")
            if not d["unspecified"] < value <= 127:
                raise ValueError("Named value must fit MIDI and differ from reset")
            if wire_schema(spec) >= 2 and value >= TYPED_OFF:
                raise ValueError("Schema 2 On values must stay below the typed-Off range")
            owners[pt] = d["id"]
    for pt, row in catalog.items():
        disposition(pt, row, owners, spec)
    for key, pts in getattr(spec, "EXTRA_MUTEX_GROUPS", []):
        if len(pts) < 2 or len(set(pts)) != len(pts) or any(pt not in owners and pt not in spec.BASE_IDS for pt in pts):
            raise ValueError(f"Invalid extra mutex group: {key}")
    validate_percussion(spec, catalog)
    return owners


def validate_percussion(spec, catalog):
    """Percussion-map codes: musical pitches only (never a keyswitch conduit or
    the beacon), unique per map, each a non-empty set of catalog IDs, and one
    code that is exactly Natural so Dorico's fallback has somewhere to land."""
    low, high = MUSICAL_RANGE
    ids = set()
    for m in percussion_maps(spec):
        if m.ENTITY_ID in ids or not m.INSTRUMENTS or not m.CODES:
            raise ValueError(f"Percussion map needs a unique ID, instruments and codes: {m.ENTITY_ID}")
        ids.add(m.ENTITY_ID)
        pitches, sets = set(), set()
        for code in m.CODES:
            pitch, pts = code[0], code[1].split("+")
            if not low <= pitch <= high or pitch in pitches:
                raise ValueError(f"Percussion code pitch must be unique and musical: {pitch}")
            if len(set(pts)) != len(pts) or any(pt not in catalog for pt in pts) or frozenset(pts) in sets:
                raise ValueError(f"Invalid or duplicate percussion code techniques: {code[1]}")
            if len(code) > 2 and not (0 <= code[2] <= 127 and not low <= code[2] <= high):
                raise ValueError(f"Percussion code keyswitch must lie outside the musical range: {code[2]}")
            pitches.add(pitch)
            sets.add(frozenset(pts))
        if frozenset(["pt.natural"]) not in sets:
            raise ValueError(f"Percussion map needs a Natural code: {m.ENTITY_ID}")


def sub(parent, tag, value=None, **attrs):
    node = ET.SubElement(parent, tag, attrs)
    if value is not None:
        node.text = str(value)
    return node


def action(parent, phase, kind, number, value):
    tag = "switchOffAction" if phase == "switchOff" else "switchOnAction"
    node = sub(parent, tag)
    sub(node, "type", "kControlChange" if kind == "CC" else "kKeySwitch")
    sub(node, "param1", number)
    sub(node, "param2", value)


def base_rows(spec, source_bases):
    """The base rows, each with its CC102 code. Without spec.BASES they are
    the universal map's 25 rows as they are. With BASES [(technique IDs,
    "cc1" | "velocity")], each entry becomes five rows (one per note-length
    bucket, codes 5i..5i+4): the universal rows for those techniques when
    it has them (their Dorico flags kept), else copies of the Natural rows
    with the techniques replaced; the dynamics policy is set either way."""
    bases = getattr(spec, "BASES", None)
    if bases is None:
        return [(int(row.findtext("baseSwitchID")), copy.deepcopy(row)) for row in source_bases]
    by_ids = {}
    for row in source_bases:
        by_ids.setdefault(row.findtext("techniqueIDs"), []).append(row)
    result = []
    for i, (ids, volume) in enumerate(bases):
        if volume not in VOLUME_TYPES:
            raise ValueError(f"Unknown dynamics policy for base {ids}: {volume}")
        rows = by_ids.get(ids)
        if rows is None:
            if any(set(k.split("+")) == set(ids.split("+")) for k in by_ids):
                raise ValueError(f"Spell the universal base as the universal map does: {ids}")
            rows = by_ids["pt.natural"]
        if [r.findtext("conditionString") for r in rows] != list(LENGTH_CONDITIONS):
            raise ValueError("Hand-built base rows are not the five length buckets in order")
        for bucket, original in enumerate(rows):
            row = copy.deepcopy(original)
            row.find("baseSwitchID").text = str(5 * i + bucket)
            row.find("techniqueIDs").text = ids
            kind, param = VOLUME_TYPES[volume]
            row.find("volumeType/type").text = kind
            row.find("volumeType/param1").text = str(param)
            result.append((5 * i + bucket, row))
    return result


def percussion_definitions(root, spec, catalog):
    """Append one DrumKitNoteMapDefinition per percussion map: every covered
    instrument gets every code, so a pitch means the same stroke on each."""
    table = []
    if not percussion_maps(spec):
        return table
    entities = sub(sub(root, "drumKitNoteMapDefinitions"), "entities", array="true")
    for m in percussion_maps(spec):
        definition = sub(entities, "DrumKitNoteMapDefinition")
        for tag, value in [("name", m.NAME), ("entityID", m.ENTITY_ID), ("parentEntityID", ""),
                           ("inheritanceMask", 0), ("version", spec.REVISION),
                           ("forSingleInstrument", "true" if m.FOR_SINGLE_INSTRUMENT else "false"),
                           ("applyStageTemplateSettings", "false")]:
            sub(definition, tag, value)
        entries = sub(definition, "entries", array="true")
        codes = []
        for code in m.CODES:
            pitch, ids = code[0], code[1]
            name = " + ".join(catalog[pt]["label"] for pt in ids.split("+"))
            codes.append(dict(pitch=pitch, techniques=ids.split("+"), name=name,
                              **({"keyswitch": code[2]} if len(code) > 2 else {})))
            for instrument in m.INSTRUMENTS:
                entry = sub(entries, "entry")
                sub(entry, "instrumentEntityID", instrument)
                sub(entry, "techniqueIDs", ids)
                sub(entry, "pitch", pitch)
                if len(code) > 2:
                    sub(entry, "keyswitches", code[2])
                sub(entry, "name", name)
        table.append(dict(entity_id=m.ENTITY_ID, name=m.NAME, for_single_instrument=bool(m.FOR_SINGLE_INSTRUMENT),
                          instruments=list(m.INSTRUMENTS), codes=codes,
                          entry_count=len(m.INSTRUMENTS) * len(m.CODES)))
    return table


def build(spec=legacy_profile):
    # The default Python API remains r2 for historical score generators/tests.
    # The CLI explicitly chooses the current revision and its output directory.
    catalog, dims = inventory(), dimensions(spec)
    owners = validate(dims, catalog, spec)
    section = getattr(spec, "SECTION", "Strings")
    label = getattr(spec, "LABEL", "Bowed orchestral strings")
    title = getattr(spec, "TITLE", "Orchestral strings")
    source = ET.parse(BASE_SOURCE).find(".//ExpressionMapDefinition")
    source_bases = source.findall("playingTechniqueCombinations/playingTechniqueCombination")
    if len(source_bases) != 25:
        raise ValueError("Hand-built base set changed; review before regenerating")
    fingerprint_data = dict(dimensions=dims, aliases=spec.ALIASES,
                            base_source_sha256=hashlib.sha256(BASE_SOURCE.read_bytes()).hexdigest())
    if hasattr(spec, "BASES"):
        fingerprint_data.update(bases=[list(b) for b in spec.BASES])
    if percussion_maps(spec):
        fingerprint_data.update(percussion=[dict(entity_id=m.ENTITY_ID, instruments=list(m.INSTRUMENTS),
                                                 codes=[list(c) for c in m.CODES],
                                                 for_single_instrument=bool(m.FOR_SINGLE_INSTRUMENT))
                                            for m in percussion_maps(spec)])
    if hasattr(spec, "INSTRUMENTS"):
        fingerprint_data.update(section=section, instruments=list(spec.INSTRUMENTS),
                                extra_mutex_groups=spec.EXTRA_MUTEX_GROUPS,
                                supported_routing="one Dorico voice per endpoint/port/channel")
    schema = wire_schema(spec)
    if schema >= 2:
        fingerprint_data.update(wire_schema=schema, typed_off=TYPED_OFF, beacon_pitches=list(BEACON_PITCHES))
    if written_durations(spec):
        fingerprint_data.update(playback_overrides=dict(PINNED_OPTIONS))
    fingerprint = hashlib.sha256(json.dumps(fingerprint_data, sort_keys=True).encode()).hexdigest()
    if schema >= 2:
        description = (f"Fiddle bridge wire schema 2: typed Off (On+{TYPED_OFF}) and revision beacon on "
                       f"KS{BEACON_PITCHES[0]}/KS{BEACON_PITCHES[1]}. "
                       + ("Note durations pinned to 100% (written lengths). " if written_durations(spec) else "")
                       + "Pending Dorico host acceptance. "
                       f"{label}: {1 + len(dims)} conduits, {len(source_bases)} bases, "
                       f"{len(owners)} add-ons. Specification SHA256: " + fingerprint)
    else:
        description = ("INSPECTION ONLY. No compatible Fiddle decoder. Do not use for playback. "
                       f"{label}: {1 + len(dims)} conduits, {len(source_bases)} bases, "
                       f"{len(owners)} add-ons. Off/cancellation policy provisional. "
                       "Specification SHA256: " + fingerprint)
    root = ET.Element("kScoreLibrary")
    sub(root, "fileVersion", "1.1438")
    definition = sub(sub(sub(root, "expressionMapDefinitions"), "entities", array="true"),
                     "ExpressionMapDefinition")
    for tag, value in [
        ("name", spec.NAME), ("entityID", spec.ENTITY_ID), ("parentEntityID", ""),
        ("inheritanceMask", 0), ("creator", "Fiddle"),
        ("description", description),
        ("version", spec.REVISION), ("pluginNames", ""), ("autoMutualExclusion", "false"),
        ("allowMultipleNotesAtSamePitch", "true"), ("applyStageTemplateSettings", "false"),
        ("pitchBendRange", 2), ("microtonalPlaybackMethod", "kAuto")]:
        sub(definition, tag, value)
    init = sub(definition, "initSwitchData")
    sub(init, "enabled", "true")
    init_actions = sub(init, "initActions", array="true")
    action(init_actions, "init", "CC", 102, 0)
    for d in dims:
        action(init_actions, "init", d["kind"], d["number"], d["unspecified"])
    beacon_velocities = beacon(spec.ENTITY_ID, spec.REVISION) if schema >= 2 else None
    if beacon_velocities:
        for pitch, velocity in zip(BEACON_PITCHES, beacon_velocities):
            action(init_actions, "init", "KS", pitch, velocity)
    bases = sub(definition, "playingTechniqueCombinations", array="true")
    base_table = []
    for value, base in base_rows(spec, source_bases):
        on = base.find("switchOnActions")
        on.clear()
        on.set("array", "true")
        action(on, "switchOn", "CC", 102, value)
        bases.append(base)
        base_table.append(dict(value=value, techniques=base.findtext("techniqueIDs").split("+"),
                               condition=base.findtext("conditionString"),
                               **({"dynamics": "cc1" if base.findtext("volumeType/type") == "kCC" else "velocity"}
                                  if hasattr(spec, "BASES") else {})))
    addons = sub(definition, "techniqueAddOns", array="true")
    index = 0
    for d in dims:
        for pt, value in d["values"].items():
            addon = sub(addons, "techniqueAddOn")
            sub(addon, "techAddOnSwitchID", index)
            sub(addon, "techniqueIDs", pt)
            sub(addon, "enabled", "true")
            action(sub(addon, "switchOnActions", array="true"), "switchOn",
                   d["kind"], d["number"], value)
            action(sub(addon, "switchOffActions", array="true"), "switchOff",
                   d["kind"], d["number"], value + TYPED_OFF if schema >= 2 else d["unspecified"])
            index += 1
    groups = sub(definition, "mutualExclusionGroups", array="true")
    # The two shortening articulations exclude each other wherever both are bases.
    group_specs = [("shortening", ["pt.staccatissimo", "pt.staccato"])] \
        if {"pt.staccatissimo", "pt.staccato"} <= set(spec.BASE_IDS) else []
    for d in dims:
        pts = list(d["values"])
        if d["natural_cancels"]:
            pts.insert(0, "pt.natural")
        if len(pts) > 1:
            group_specs.append((d["id"], pts))
    group_specs.extend(getattr(spec, "EXTRA_MUTEX_GROUPS", []))
    for key, pts in group_specs:
        group = sub(groups, "mutualExclusionGroup")
        sub(group, "groupID", f"ptmg.user.Fiddle.Orchestral{section}.r{spec.REVISION}.{key}")
        sub(group, "name", section + ": " + key.replace("_", " "))
        sub(group, "techniqueIDs", ", ".join(pts))
    overrides = sub(definition, "playbackOptionsOverrides", array="true")
    if written_durations(spec):
        for option, value in PINNED_OPTIONS:
            row = sub(overrides, "optionsOverride")
            sub(row, "option", option)
            sub(row, "value", value)
    percussion = percussion_definitions(root, spec, catalog)
    ET.indent(root, space="  ")
    xml = ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"
    counts = dict(base_switches=len(base_table), add_on_switches=index,
                  switch_rows=len(base_table) + index, init_switches=1,
                  explicit_pt_ids=len(spec.BASE_IDS) + len(owners), aliases=len(spec.ALIASES),
                  addon_dimensions=len(dims), conduits=1 + len(dims),
                  cc_conduits=1 + sum(d["kind"] == "CC" for d in dims),
                  ks_conduits=sum(d["kind"] == "KS" for d in dims),
                  spare_conduits=len(conduit_pool(spec)) - len(dims), mutex_groups=len(group_specs))
    budget = len(conduit_pool(spec)) + 1
    manifest = dict(schema_version=1, revision=spec.REVISION, name=spec.NAME, entity_id=spec.ENTITY_ID,
                    runtime_compatible=schema >= 2, dorico_import_verified=False,
                    **({"written_durations": True} if written_durations(spec) else {}),
                    musical_midi_range=[21, 114], counts=counts,
                    specification_sha256=fingerprint, map_sha256=hashlib.sha256(xml).hexdigest(),
                    base_cc=102, bases=base_table, dimensions=dims, aliases=spec.ALIASES)
    if schema >= 2:
        manifest.update(wire_schema=schema, conduit_budget=budget, typed_off=TYPED_OFF,
                        beacon=dict(pitches=list(BEACON_PITCHES), velocities=list(beacon_velocities)))
    if hasattr(spec, "INSTRUMENTS"):
        manifest.update(section=section, instruments=list(spec.INSTRUMENTS),
                        supported_routing="one Dorico voice per endpoint/port/channel",
                        title=title)
    if percussion:
        counts["pitch_code_ids"] = len(pitch_codes(spec))
        manifest.update(percussion=percussion)
    return {f"Fiddle-Orchestral-{section}-r{spec.REVISION}.doricolib": xml,
            "allocation.json": (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode(),
            "allocation.md": allocation_markdown(manifest, catalog).encode(),
            "inventory.md": inventory_markdown(catalog, owners, spec).encode()}


def allocation_markdown(m, catalog):
    c = m["counts"]
    typed = m.get("wire_schema", 1) >= 2
    status = ("Wire schema 2 candidate: compiled by Fiddle, **not yet host-verified**."
              if typed else "Inspection prototype, **not runtime compatible or host-verified**.")
    lines = [f"# {m.get('title', 'Orchestral strings')} r{m['revision']} — generated allocation", "",
             status + " See [README](README.md) for scope, cancellation policy and caveats.", "",
             f"**{c['conduits']}/{m.get('conduit_budget', 52)} conduits: {c['cc_conduits']} CCs + {c['ks_conduits']} KS pitches; "
             f"{c['spare_conduits']} spare.** {c['base_switches']} bases + {c['add_on_switches']} add-ons = "
             f"{c['switch_rows']} switch rows, plus one Init switch ({c['conduits'] + (2 if typed else 0)} actions"
             + (", including the two beacon keyswitches). " if typed else "). ") +
             f"{c['explicit_pt_ids']} distinct encoded IDs + {c['aliases']} built-in alias"
             + (f" + {c['pitch_code_ids']} IDs carried by percussion-map pitch codes" if "pitch_code_ids" in c else "")
             + f"; {c['mutex_groups']} explicit mutex groups.", "",
             "All pitch numbers are MIDI integers, not octave labels. CC value 0 means unspecified "
             "except CC102 (base code 0); KS velocity 1 means unspecified. Physical KS releases "
             "are not dimension resets. " + (
                 f"Every add-on Off writes its typed value On+{m['typed_off']}: it clears the dimension "
                 "only while that technique is current. Init writes the unspecified value. "
                 f"Init also sends the revision beacon KS{m['beacon']['pitches'][0]} velocity "
                 f"{m['beacon']['velocities'][0]} and KS{m['beacon']['pitches'][1]} velocity "
                 f"{m['beacon']['velocities'][1]}; those pitches are reserved and carry no technique."
                 if typed else "Every add-on Off writes its own unspecified value."), "",
             "## Base (CC102)", "", "Copied articulation/length codes; the old CC103 reset is removed."]
    dynamics = any("dynamics" in b for b in m["bases"])
    if dynamics:
        lines += ["Dynamics: cc1 = the level arrives on CC1 just after the note-on; velocity = in the note-on.", "",
                  "| Value | PTs | Condition | Dynamics |", "| --- | --- | --- | --- |"]
    else:
        lines += ["", "| Value | PTs | Condition |", "| --- | --- | --- |"]
    for b in m["bases"]:
        lines.append(f"| {b['value']} | {' + '.join(b['techniques'])} | `{b['condition']}` |"
                     + (f" {b['dynamics']} |" if dynamics else ""))
    for p in m.get("percussion", []):
        lines += ["", f"## Percussion map `{p['entity_id']}`", "",
                  f"{p['name']}: {len(p['codes'])} pitch codes × {len(p['instruments'])} instruments = "
                  f"{p['entry_count']} entries; forSingleInstrument {str(p['for_single_instrument']).lower()}. "
                  "Dorico picks the entry matching a note's techniques and sends its pitch, so the pitch names "
                  "the stroke and the chair names the instrument. Every instrument has the same table.", "",
                  "| Pitch | PTs | Keyswitch |", "| --- | --- | --- |"]
        for c in p["codes"]:
            labels = " + ".join(f"{catalog[pt]['label']} (`{pt}`)" for pt in c["techniques"])
            lines.append(f"| {c['pitch']} | {labels} | {c.get('keyswitch', '—')} |")
        lines += ["", "Instruments: " + ", ".join(f"`{i}`" for i in p["instruments"])]
    lines += ["", "## Add-on dimensions", "",
              "Each multi-value row is one mutex group. Singletons are independent; "
              "those marked nat. also have a two-member cancellation group with Natural. "
              "Tenuto is deliberately not in the shortening mutex group.", "",
              "| Dimension | Conduit | " + ("Init" if typed else "Off / Init") + " | Named On values | Explicit nat. cancels? |",
              "| --- | --- | --- | --- | --- |"]
    for d in m["dimensions"]:
        values = "; ".join(f"{v}: {catalog[pt]['label']} (`{pt}`)" for pt, v in d["values"].items())
        lines.append(f"| {d['id']} | {d['kind']}{d['number']} | {d['unspecified']} | {values} | "
                     f"{'Proposed yes' if d['natural_cancels'] else 'No'} |")
    lines += ["", "## Grouping rationale", ""]
    lines += [f"- **{d['id']}**: {d['rationale']}" for d in m["dimensions"]]
    return "\n".join(lines) + "\n"


def inventory_markdown(catalog, owners, spec=legacy_profile):
    lines = [f"# {getattr(spec, 'TITLE', 'Orchestral strings')} r{spec.REVISION} — full built-in inventory disposition", "",
             "All 298 exact IDs from the recorded Dorico 6.2 inventory are accounted for. "
             "The family classification and section scope are our review decisions, not a "
             "Dorico-supplied instrument compatibility guarantee. No custom PT definitions are imported.", "",
             "Coverage means a switch can transmit the ID **if Dorico activates it**; it does "
             "not establish that a particular score symbol or continuous dynamic emits that PT. "
             "Out-of-scope entries are visible for review, not silently discarded for capacity.", "",
             "| Exact ID | Label | Kind | Disposition | Encoding / reason |",
             "| --- | --- | --- | --- | --- |"]
    for pt, row in sorted(catalog.items()):
        status, reason = disposition(pt, row, owners, spec)
        lines.append(f"| `{pt}` | {row['label']} | {row['kind']} | {status} | {reason} |")
    return "\n".join(lines) + "\n"


def check_dorico(path):
    elements = ET.parse(path).findall(".//PlayingTechniqueDefinition")
    source = {e.findtext("entityID"): e for e in elements}
    recorded = inventory()
    if source.keys() != recorded.keys():
        raise ValueError("Installed Dorico PT IDs differ from the recorded inventory")
    for pt, row in recorded.items():
        e = source[pt]
        if e.findtext("name").strip() != row["label"]:
            raise ValueError(f"Label differs for {pt}")
    for pt, target in ALIASES.items():
        if source[pt].findtext("aliasForPlayingTechniqueID") != target:
            raise ValueError(f"Alias changed for {pt}")
    print(f"Verified all {len(source)} built-in IDs/labels and Bowed alias against {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--dorico-catalog", type=Path)
    parser.add_argument("--revision", type=int, choices=(2, 3, 4, 5), default=5,
                        help="Default: current r5; r2-r4 reproduce frozen artifacts")
    args = parser.parse_args()
    if args.dorico_catalog:
        check_dorico(args.dorico_catalog)
    if args.revision == 5:
        import profile_r5 as spec
        destination = HERE / "r5"
    elif args.revision == 4:
        import profile_r4 as spec
        destination = HERE / "r4"
    elif args.revision == 3:
        import profile_r3 as spec
        destination = HERE / "r3"
    else:
        spec, destination = legacy_profile, HERE
    outputs = build(spec)
    if not args.check:
        destination.mkdir(exist_ok=True)
    for name, content in outputs.items():
        path = destination / name
        if args.check:
            if not path.exists() or path.read_bytes() != content:
                raise SystemExit(f"Stale/missing generated artifact: {path}")
        else:
            path.write_bytes(content)
    print(("Checked" if args.check else "Generated") + f" {len(outputs)} artifacts")


if __name__ == "__main__":
    main()
