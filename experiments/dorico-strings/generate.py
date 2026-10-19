#!/usr/bin/env python3
"""Generate isolated Dorico wire probes and exact-ID decoder tables; stdlib only.

This is not the production bridge generator. None of these maps can be used by
Fiddle's legacy decoder. No installation, runtime configuration, or user data
is changed. Use --check to check checked-in generated artifacts without writes.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
VARIANTS = ("separate", "packed", "keyswitch", "velocity", "dimensions", "high-vibrato")
KEY_SPEC_FILES = {"keyswitch": "keyswitch-probe.json",
                  "velocity": "velocity-probe.json",
                  "dimensions": "dimensions-probe.json",
                  "high-vibrato": "high-vibrato-probe.json"}


def load_spec(path=HERE / "profile.json"):
    spec = json.loads(path.read_text())
    if spec["schema_version"] != 1:
        raise ValueError("Unsupported specification schema")
    if type(spec.get("map_revision")) is not int or spec["map_revision"] < 1:
        raise ValueError("Positive map revision required")
    ccs = [spec["base_cc"]] + [a["cc"] for a in spec["axes"]]
    if len(ccs) != len(set(ccs)) or any(type(c) is not int or not 102 <= c <= 119 for c in ccs):
        raise ValueError("Budget has duplicate or non-reserved controllers")
    axes = spec["axes"]
    if len({a["id"] for a in axes}) != len(axes):
        raise ValueError("Duplicate axis ID")
    ids = []
    for values in [spec[k] for k in ("shortening", "tenuto", "accent")] + [a["values"] for a in axes]:
        if not values or values[0] is not None or len(values) > 128 or len(set(values)) != len(values):
            raise ValueError("Each axis needs unique values and unspecified at zero")
        ids.extend(v for v in values if v is not None)
    if len(ids) != len(set(ids)) or any(not v.startswith("pt.") for v in ids):
        raise ValueError("PTs must have unique ownership and exact pt. IDs")
    if spec["lengths"] != ["kVeryShort", "kShort", "kMedium", "kLong", "kVeryLong"]:
        raise ValueError("Unexpected length categories")
    size = len(spec["shortening"]) * len(spec["tenuto"]) * len(spec["accent"]) * len(spec["lengths"])
    if size > 128:
        raise ValueError("Base product does not fit a CC")
    packed = spec["packed_probe"]
    members = packed["axis_ids"]
    if len(members) != len(set(members)) or not members:
        raise ValueError("Duplicate/empty packed member list")
    by_id = {a["id"]: a for a in axes}
    if any(m not in by_id or not by_id[m]["prototype"] or len(by_id[m]["values"]) != 2 for m in members):
        raise ValueError("Packed probe requires binary prototype axes")
    if packed["cc"] != by_id[members[0]]["cc"] or 2 ** len(members) > 128:
        raise ValueError("Invalid packed controller or state count")
    return spec


def load_key_spec(spec, path=HERE / "keyswitch-probe.json"):
    probe = json.loads(path.read_text())
    if probe["schema_version"] not in (1, 2) or type(probe["map_revision"]) is not int or probe["map_revision"] < 1:
        raise ValueError("Invalid keyswitch schema/revision")
    low, high = probe["musical_range"]
    if any(type(v) is not int for v in (low, high)) or not 0 <= low <= high <= 127:
        raise ValueError("Invalid musical range")
    velocities = ([probe["velocity"]] if probe["schema_version"] == 1 else
                  [m[k] for m in probe["modifiers"] for k in ("on_velocity", "off_velocity")])
    if any(type(v) is not int or not 1 <= v <= 127 for v in velocities):
        raise ValueError("Invalid keyswitch velocity")
    axes = {a["id"]: a for a in spec["axes"]}
    members = [m["axis_id"] for m in probe["modifiers"]]
    if not members or len(set(members)) != len(members) or any(
            m not in axes or not axes[m]["prototype"] or len(axes[m]["values"]) != 2 for m in members):
        raise ValueError("Keyswitches require unique binary prototype axes")
    notes = [m[k] for m in probe["modifiers"] for k in ("on_note", "off_note")]
    commands = notes if probe["schema_version"] == 1 else list(zip(notes, velocities))
    if len(set(commands)) != len(commands) or any(type(n) is not int or not 0 <= n <= 127 or low <= n <= high for n in notes):
        raise ValueError("Keyswitch commands must be unique and their pitches outside the musical range")
    for dimension in probe.get("dimensions", []):
        axis_id, note, values = dimension["axis_id"], dimension["note"], dimension["velocities"]
        if (probe["schema_version"] != 2 or axis_id in members or axis_id not in axes
                or not axes[axis_id]["prototype"] or len(values) != len(axes[axis_id]["values"])
                or len(set(values)) != len(values)
                or any(type(v) is not int or not 1 <= v <= 127 for v in values)
                or type(note) is not int or not 0 <= note <= 127 or low <= note <= high or note in notes):
            raise ValueError("Invalid or colliding keyswitch dimension")
        members.append(axis_id)
        notes.append(note)
    return probe


def states(spec, variant, key_spec=None):
    if variant not in VARIANTS:
        raise ValueError("Unknown variant")
    tables = {}
    base = {}
    for s, t, a, length in itertools.product(range(len(spec["shortening"])), range(len(spec["tenuto"])), range(len(spec["accent"])), range(5)):
        value = (((s * len(spec["tenuto"]) + t) * len(spec["accent"]) + a) * 5 + length)
        pts = [spec["shortening"][s], spec["tenuto"][t], spec["accent"][a]]
        base[str(value)] = {"techniques": sorted(p for p in pts if p), "length": spec["lengths"][length]}
    tables[str(spec["base_cc"])] = {"name": "articulation_and_length", "values": base}
    packed_ids = spec["packed_probe"]["axis_ids"] if variant == "packed" else []
    key_ids = []
    if variant in KEY_SPEC_FILES:
        key_spec = key_spec or variant_key_spec(spec, variant)
        key_ids = [m["axis_id"] for m in key_spec["modifiers"]]
        key_ids += [d["axis_id"] for d in key_spec.get("dimensions", [])]
    for axis in spec["axes"]:
        if axis["prototype"] and axis["id"] not in packed_ids + key_ids:
            tables[str(axis["cc"])] = {"name": axis["id"], "values": {str(i): {"techniques": [p] if p else []} for i, p in enumerate(axis["values"])}}
    if packed_ids:
        by_id = {a["id"]: a for a in spec["axes"]}
        tables[str(spec["packed_probe"]["cc"])] = {
            "name": "packed_string_modifiers",
            "values": {str(mask): {"techniques": sorted(by_id[m]["values"][1] for bit, m in enumerate(packed_ids) if mask & (1 << bit))} for mask in range(2 ** len(packed_ids))},
        }
    return tables


def sub(parent, tag, text=None, **attrs):
    element = ET.SubElement(parent, tag, attrs)
    if text is not None:
        element.text = str(text)
    return element


def actions(parent, kind, values, action_type="kControlChange"):
    container = sub(parent, kind + "Actions", array="true")
    tag = "switchOffAction" if kind == "switchOff" else "switchOnAction"
    for cc, value in values:
        action = sub(container, tag)
        sub(action, "type", action_type)
        sub(action, "param1", cc)
        sub(action, "param2", value)
    return container


def variant_key_spec(spec, variant):
    if variant not in KEY_SPEC_FILES:
        return None
    return load_key_spec(spec, HERE / KEY_SPEC_FILES[variant])


def render(spec, variant):
    key_spec = variant_key_spec(spec, variant)
    tables = states(spec, variant, key_spec)
    identity = {"schema": 1, "spec": spec, "variant": variant}
    if key_spec:
        identity.update(schema=2, keyswitch_probe=key_spec)
    fingerprint = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    revision = key_spec["map_revision"] if key_spec else spec["map_revision"]
    entity_id = "xmap.user.Fiddle.Experimental.Strings.v1." + variant + ".r" + str(revision)
    root = ET.Element("kScoreLibrary")
    sub(root, "fileVersion", "1.1438")
    entities = sub(sub(root, "expressionMapDefinitions"), "entities", array="true")
    definition = sub(entities, "ExpressionMapDefinition")
    for tag, value in {
        "name": "Fiddle EXP Strings " + {"separate": "A - separate", "packed": "B - packed", "keyswitch": "C - keyswitch", "velocity": "D - velocity", "dimensions": "E - dimensions", "high-vibrato": "F - high vibrato"}[variant] + " r" + str(revision),
        "entityID": entity_id, "parentEntityID": None, "inheritanceMask": 0,
        "creator": "Fiddle", "description": "WIRE TEST ONLY. Incompatible with installed Fiddle decoder. " + fingerprint,
        "version": revision, "pluginNames": None, "autoMutualExclusion": "false",
        "allowMultipleNotesAtSamePitch": "true" if variant in ("velocity", "dimensions", "high-vibrato") else "false", "applyStageTemplateSettings": "false",
        "pitchBendRange": 2, "microtonalPlaybackMethod": "kAuto",
    }.items():
        sub(definition, tag, value)
    init = sub(definition, "initSwitchData")
    sub(init, "enabled", "true")
    init_actions = actions(init, "init", [(int(cc), 0) for cc in tables])
    if key_spec:
        for modifier in key_spec["modifiers"]:
            action = sub(init_actions, "switchOnAction")
            sub(action, "type", "kKeySwitch")
            sub(action, "param1", modifier["off_note"])
            sub(action, "param2", modifier.get("off_velocity", key_spec.get("velocity")))
        for dimension in key_spec.get("dimensions", []):
            action = sub(init_actions, "switchOnAction")
            sub(action, "type", "kKeySwitch")
            sub(action, "param1", dimension["note"])
            sub(action, "param2", dimension["velocities"][0])
    bases = sub(definition, "playingTechniqueCombinations", array="true")
    for value, state in tables[str(spec["base_cc"])]["values"].items():
        base = sub(bases, "playingTechniqueCombination")
        for tag, text in {
            "baseSwitchID": value,
            # Dorico switch conjunctions use '+'. Commas are for MEG lists.
            # Fiddle's permissive parser accepts both, so it cannot validate
            # this host-specific distinction for us.
            "techniqueIDs": "+".join(state["techniques"] or ["pt.natural"]),
            "enabled": "true", "flags": 0,
            "conditionString": "NoteLength > kLong" if state["length"] == "kVeryLong" else "NoteLength == " + state["length"],
            "velocityRange": "1,127", "pitchRange": "0,127", "transpose": 0,
            "ticksBefore": 0, "millisecondsBefore": 0, "velocityFactor": "1.000000",
            "lengthFactor": "1.000000", "monophonic": "false",
            "applyMillisecondsBeforeToEndOffsets": "true", "applyMillisecondsBeforeToControllers": "true",
        }.items():
            sub(base, tag, text)
        sub(sub(base, "volumeType"), "type", "kCC")
        sub(base.find("volumeType"), "param1", 1)
        sub(sub(base, "attackType"), "type", "kNoteVelocity")
        sub(base.find("attackType"), "param1", 0)
        actions(base, "switchOn", [(spec["base_cc"], int(value))])
        # No note-off reset: this channel-global byte is set by each base switch.
        actions(base, "switchOff", [])
    addons = sub(definition, "techniqueAddOns", array="true")
    index = 0
    for cc, table in tables.items():
        if int(cc) == spec["base_cc"]:
            continue
        for value, state in table["values"].items():
            if not state["techniques"]:
                continue
            addon = sub(addons, "techniqueAddOn")
            sub(addon, "techAddOnSwitchID", index)
            sub(addon, "techniqueIDs", "+".join(state["techniques"]))
            sub(addon, "enabled", "true")
            actions(addon, "switchOn", [(int(cc), int(value))])
            actions(addon, "switchOff", [(int(cc), 0)])
            index += 1
    key_switches = []
    if key_spec:
        by_id = {a["id"]: a for a in spec["axes"]}
        for modifier in key_spec["modifiers"]:
            pt = by_id[modifier["axis_id"]]["values"][1]
            key_switches.append({**modifier, "technique": pt})
            addon = sub(addons, "techniqueAddOn")
            sub(addon, "techAddOnSwitchID", index)
            sub(addon, "techniqueIDs", pt)
            sub(addon, "enabled", "true")
            actions(addon, "switchOn", [(modifier["on_note"], modifier.get("on_velocity", key_spec.get("velocity")))], "kKeySwitch")
            actions(addon, "switchOff", [(modifier["off_note"], modifier.get("off_velocity", key_spec.get("velocity")))], "kKeySwitch")
            index += 1
    key_dimensions = []
    if key_spec:
        for dimension in key_spec.get("dimensions", []):
            axis = by_id[dimension["axis_id"]]
            values = dict(zip(map(str, dimension["velocities"]), axis["values"]))
            key_dimensions.append({"axis_id": axis["id"], "note": dimension["note"], "values": values})
            for velocity, pt in values.items():
                if pt is None:
                    continue
                addon = sub(addons, "techniqueAddOn")
                sub(addon, "techAddOnSwitchID", index)
                sub(addon, "techniqueIDs", pt)
                sub(addon, "enabled", "true")
                actions(addon, "switchOn", [(dimension["note"], int(velocity))], "kKeySwitch")
                actions(addon, "switchOff", [(dimension["note"], dimension["velocities"][0])], "kKeySwitch")
                index += 1
    megs = sub(definition, "mutualExclusionGroups", array="true")
    groups = [("shortening", spec["shortening"][1:]), ("accent", spec["accent"][1:])]
    for axis in spec["axes"]:
        if axis["prototype"] and (len(axis["values"]) > 2 or axis["natural_cancels"]):
            groups.append((axis["id"], (["pt.natural"] if axis["natural_cancels"] else []) + axis["values"][1:]))
    for name, pts in groups:
        meg = sub(megs, "mutualExclusionGroup")
        sub(meg, "groupID", "ptmg.user.Fiddle.Experimental.Strings.v1." + name + ".r" + str(revision))
        sub(meg, "name", "Fiddle experiment: " + name)
        sub(meg, "techniqueIDs", ", ".join(pts))
    sub(definition, "playbackOptionsOverrides", array="true")
    ET.indent(root, space="  ")
    xml = ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"
    manifest = {
        "schema_version": 1, "profile_id": spec["profile_id"], "variant": variant, "map_revision": revision,
        "runtime_compatible": False, "dorico_verified": False,
        "spec_fingerprint": fingerprint, "map_entity_id": entity_id,
        "map_file": variant + ".doricolib", "map_sha256": hashlib.sha256(xml).hexdigest(),
        "base_cc": spec["base_cc"], "tables": tables,
        "reset_policy": "Init sets emitted CCs to zero; add-on off sets its CC to zero. Host restoration/order must be tested.",
    }
    if key_spec:
        manifest.update(schema_version=2, key_switches=key_switches,
                        musical_range=key_spec["musical_range"],
                        reset_policy="Init sends CC zero and explicit OFF command notes. Positive-velocity command note-ons latch state; releases have no semantic effect. Missing initialization is unknown, not OFF.")
        if key_spec["schema_version"] == 2:
            manifest.update(schema_version=3, reset_policy="Init sends CC zero and explicit OFF pitch/velocity commands. Only exact positive-velocity pairs change state; unknown velocities warn. Releases have no semantic effect; missing initialization stays unknown.")
        if key_dimensions:
            manifest.update(schema_version=4, key_dimensions=key_dimensions,
                            reset_policy="Init establishes explicit binary OFF and scalar unspecified states. Scalar commands replace the complete dimension; switch-off sends unspecified. Physical releases do not reset or restore state. Exact pitch/velocity matching; unknown or missing states are not guessed.")
    return xml, (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode()


def score_scaffold():
    """Portable notation scaffold, not a .dorico file or PT activation proof."""
    root = ET.Element("score-partwise", version="3.1")
    sub(sub(root, "work"), "work-title", "Fiddle Strings wire experiment — disposable test only")
    part = sub(sub(root, "part-list"), "score-part", id="P1")
    sub(part, "part-name", "Violin")
    instrument = sub(part, "score-instrument", id="P1-I1")
    sub(instrument, "instrument-name", "Violin")
    midi_instrument = sub(part, "midi-instrument", id="P1-I1")
    sub(midi_instrument, "midi-channel", 1)
    sub(midi_instrument, "midi-program", 41)
    music = sub(root, "part", id="P1")
    articulations = {2: ["staccato"], 3: ["tenuto"], 4: ["staccato", "tenuto"],
                     5: ["staccato", "tenuto", "accent"],
                     6: ["staccatissimo", "strong-accent"], 8: ["accent"]}
    for bar in range(4):
        measure = sub(music, "measure", number=str(bar + 1))
        if bar == 0:
            attributes = sub(measure, "attributes")
            sub(attributes, "divisions", 1)
            sub(sub(attributes, "key"), "fifths", 0)
            time = sub(attributes, "time")
            sub(time, "beats", 4)
            sub(time, "beat-type", 4)
            clef = sub(attributes, "clef")
            sub(clef, "sign", "G")
            sub(clef, "line", 2)
            tempo = sub(measure, "direction", placement="above")
            metronome = sub(sub(tempo, "direction-type"), "metronome")
            sub(metronome, "beat-unit", "quarter")
            sub(metronome, "per-minute", 120)
            sub(tempo, "sound", tempo="120")
        for beat in range(4):
            index = bar * 4 + beat + 1
            marker = sub(measure, "direction", placement="above")
            sub(sub(marker, "direction-type"), "words", "T" + str(index))
            note = sub(measure, "note")
            pitch = sub(note, "pitch")
            sub(pitch, "step", "C")
            sub(pitch, "octave", 5)
            sub(note, "duration", 1)
            sub(note, "type", "quarter")
            if index in articulations:
                container = sub(sub(note, "notations"), "articulations")
                for name in articulations[index]:
                    sub(container, name)
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def artifacts(spec):
    out = {}
    for variant in VARIANTS:
        xml, manifest = render(spec, variant)
        out[variant + ".doricolib"] = xml
        out[variant + ".decoder.json"] = manifest
    out["acceptance.musicxml"] = score_scaffold()
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--output-dir", type=Path, default=HERE / "generated")
    parser.add_argument("--builtins", type=Path, help="Optional installed playingTechniqueDefinitions.xml for exact-ID validation")
    args = parser.parse_args()
    spec = load_spec()
    if args.builtins:
        ids = {e.findtext("entityID") for e in ET.parse(args.builtins).findall(".//PlayingTechniqueDefinition")}
        declared = set(itertools.chain(*(a["values"][1:] for a in spec["axes"])))
        declared.update(itertools.chain(*(spec[k][1:] for k in ("shortening", "tenuto", "accent"))))
        declared.update(itertools.chain(*spec["deferred"].values()))
        declared.update(spec["alias_notes"])
        if declared - ids:
            parser.error("Unknown built-in IDs: " + ", ".join(sorted(declared - ids)))
    if not args.check:
        args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, data in artifacts(spec).items():
        path = args.output_dir / name
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                parser.error("Stale or missing generated artifact: " + str(path))
        else:
            path.write_bytes(data)
        print(("Checked " if args.check else "Generated ") + str(path))


if __name__ == "__main__":
    main()
