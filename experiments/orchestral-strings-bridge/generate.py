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

# Playback-option overrides that pin every note-duration category to 100%, so
# Dorico sends written lengths and Fiddle applies the duration percentages
# (the layer map's overrides, else its copy of the project's options). Dorico
# must not shorten same-pitch notes either: Fiddle ends each one before its
# same-pitch successor itself, after scaling.
DURATION_OPTIONS = tuple(f"timingOptions.{name}DurationPercent" for name in
                         ("note", "staccato", "staccatissimo", "legato", "tenuto", "marcato"))
PINNED_OPTIONS = tuple((option, "int: 100") for option in DURATION_OPTIONS) + (
    ("timingOptions.alwaysTruncateAbuttingNotesOfSamePitch", "bool: false"),)


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


def disposition(pt, row, owners, spec=legacy_profile):
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
    return owners


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
    for original in source_bases:
        base = copy.deepcopy(original)
        value = int(base.findtext("baseSwitchID"))
        on = base.find("switchOnActions")
        on.clear()
        on.set("array", "true")
        action(on, "switchOn", "CC", 102, value)
        bases.append(base)
        base_table.append(dict(value=value, techniques=base.findtext("techniqueIDs").split("+"),
                               condition=base.findtext("conditionString")))
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
    group_specs = [("shortening", ["pt.staccatissimo", "pt.staccato"])]
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
             f"{c['explicit_pt_ids']} distinct encoded IDs + {c['aliases']} built-in alias; "
             f"{c['mutex_groups']} explicit mutex groups.", "",
             "All pitch numbers are MIDI integers, not octave labels. CC value 0 means unspecified "
             "except CC102 (base code 0); KS velocity 1 means unspecified. Physical KS releases "
             "are not dimension resets. " + (
                 f"Every add-on Off writes its typed value On+{m['typed_off']}: it clears the dimension "
                 "only while that technique is current. Init writes the unspecified value. "
                 f"Init also sends the revision beacon KS{m['beacon']['pitches'][0]} velocity "
                 f"{m['beacon']['velocities'][0]} and KS{m['beacon']['pitches'][1]} velocity "
                 f"{m['beacon']['velocities'][1]}; those pitches are reserved and carry no technique."
                 if typed else "Every add-on Off writes its own unspecified value."), "",
             "## Base (CC102)", "", "Copied articulation/length codes; the old CC103 reset is removed.", "",
             "| Value | PTs | Condition |", "| --- | --- | --- |"]
    for b in m["bases"]:
        lines.append(f"| {b['value']} | {' + '.join(b['techniques'])} | `{b['condition']}` |")
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
