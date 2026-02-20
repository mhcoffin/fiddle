# Fiddle Setup

## Background

Currently, Fiddle generates a Dorico Setup file that contains a fairly small number of instruments.  This is the contents of PluginPresetLibraries/Fiddle/presets.xml and presets_for_instruments.xml. I want to create UI elements that allow the user to create their own set of instruments. A new tab will be added to the Fiddle UI for this purpose. This tab will be called "Setup".

## Proposal

A new panel will be added to the Fiddle UI. This panel will allow the user to create and manage a list of supported instruments. The primary purpose of this list is to tell Dorico which instruments the plugin supports (via `presets.xml` and `presets_for_instruments.xml`). The panel will provide controls to add and remove instruments from this list. Additional per-instrument configuration may be needed in the future, but the scope is not yet known.

The master list of instruments is part of the Dorico application installation. The Dorico application can be found at /Applications/Dorico <version>.app. We should use the most recent version of Dorico installed on the system. The master list of instruments is in the file /Applications/Dorico <version>.app/Contents/Resources/instruments.xml. 

Since there are a very large number of instruments, the panel should have a search box that allows the user to search for instruments by name. The panel should also have a way to filter the list of instruments by family (e.g. woodwinds, brass, strings, percussion, etc.) The hierarchical family structure of an instrument can be extracted from the 'musicXMLSoundID' field, which is a period-separated string.

We also need to filter by solo vs. section. The solo/section distinction is encoded directly in the instrument's entityID.

The panel should have a way to say "Done", which should generate a Dorico Setup file that contains the instruments in the list. The panel should also have a way to say "Cancel", which should discard any changes made to the list.

This result of this will be a "master list" of available instruments. We need enough information to create Dorico Setup files (`presets.xml` and `presets_for_instruments.xml`) from this list. We might need to consult the Dorico list of instruments (see below) to determine some information. It is expected that this list will be updated relatively infrequently. E.g., a users will add all the instruments they use and only update the list infrequently.

We will also need to create project-specific collections of instruments. This will provide a view of the instruments actually used in a project. This list will be updated more frequently and will not affect `presets.xml` and `presets_for_instruments.xml`. Instead, we need to persist this list in a project file (along with other settings to be determined). 

## Dorico Setup File Structure

The existing code in `DoricoConfigGenerator` already generates both files from a list of instrument assignments. The structure is as follows:

### `presets.xml`

Each `<Preset>` entry contains:
- `<Name>` — Display name (e.g., "Clarinet")
- `<Category>` — Grouping for the Dorico UI (e.g., "Woodwinds", "Brass", "Strings")
- `<ExpressionMap>` — Expression map ID
- `<Address>` — MIDI routing: `<Program>`, `<BankMSB>`, `<BankLSB>`, `<URI/>`
- Optionally `<DrumKitNoteMap>` for percussion instruments

Program/Bank values are assigned automatically by `DoricoConfigGenerator::generateAssignments()`: programs are assigned sequentially 1–128, then BankLSB increments and programs restart at 1.

### `presets_for_instruments.xml`

Maps Dorico instrument entity IDs to preset names. Each `<PresetsForInstrument>` entry contains:
- `<Instrument>` — A Dorico entity ID (e.g., `instrument.wind.flutes.flute`)
- `<Presets>` — Contains a preferred `<Preset>` name linking back to `presets.xml`

A single preset may map to multiple Dorico entity IDs (e.g., all clarinet variants may share the same sound).

### Mapping from `instruments.xml`

The fields needed for generation can be derived from Dorico's `instruments.xml` entries:
- **Display name** ← `<name>` element
- **Category** ← first segment of `<musicXMLSoundID>` (e.g., `wind`, `brass`, `strings`)
- **Entity IDs** ← `<entityID>` element (e.g., `instrument.wind.flutes.flute`)




