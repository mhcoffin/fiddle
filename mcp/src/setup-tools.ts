// Setup tools: plug-ins, libraries, maps, chairs, kits, layers and the Dorico
// playback template (docs/mcp-control.md). Arguments are snake_case here and
// camelCase on the wire to Fiddle.
import type { McpServer } from '@modelcontextprotocol/server';
import * as z from 'zod/v4';

import { callFiddleTool } from './tool-results.js';

const readOnly = {
  readOnlyHint: true, destructiveHint: false, idempotentHint: true,
  openWorldHint: false
};
const additive = {
  readOnlyHint: false, destructiveHint: false, idempotentHint: false,
  openWorldHint: false
};
const settable = {
  readOnlyHint: false, destructiveHint: false, idempotentHint: true,
  openWorldHint: false
};
const destructive = {
  readOnlyHint: false, destructiveHint: true, idempotentHint: false,
  openWorldHint: false
};

/** Drop undefined values so Fiddle sees only the fields the agent gave. */
function defined(values: Record<string, unknown>): Record<string, unknown> {
  return Object.fromEntries(
    Object.entries(values).filter(([, value]) => value !== undefined));
}

const role = z.enum(['solo', 'section']);

const libraryPatch = z.object({
  id: z.string().min(1).optional()
    .describe('An existing patch to change; omit to add a new patch'),
  name: z.string().min(1).optional().describe('Required for a new patch'),
  instrument_entity_id: z.string().optional()
    .describe('Canonical Dorico instrument ID from fiddle_search_dorico_instruments; "" clears it'),
  family: z.string().optional(),
  character: z.enum(['', 'solo', 'section', 'ensemble', 'overlay']).optional(),
  plugin_uid: z.number().int().optional()
    .describe('Instrument plug-in UID from fiddle_list_plugins; defaults to the library\'s plugin_uid for new patches. Changing it drops the captured player state.'),
  expression_map_id: z.string().optional()
    .describe('A map in Fiddle (fiddle_list_maps) or in expression_map_path; "" for none'),
  expression_map_path: z.string().optional()
    .describe('.doricolib file to import expression_map_id from, when Fiddle lacks it'),
  percussion_map_id: z.string().optional()
    .describe('A percussion map in Fiddle or in percussion_map_path; "" for none'),
  percussion_map_path: z.string().optional(),
  percussion_play_as: z.record(z.string(), z.string()).optional()
    .describe('Chair instrument ID -> the percussion map instrument a layer from this patch plays as'),
  expected_preset_name: z.string().optional()
    .describe('Preset the user must load in the player; the patch then waits for guided capture in Library Manager')
});

export function registerSetupTools(server: McpServer): void {
  // ── Discovery ──────────────────────────────────────────────────────────

  server.registerTool('fiddle_search_dorico_instruments', {
    title: 'Search Dorico instruments',
    description: 'Find canonical Dorico instrument IDs for chairs and library patches. Every word must match the name or ID. Results give the family, the suggested chair role, and, for a Dorico kit, its instruments.',
    inputSchema: z.object({
      query: z.string().optional().describe('Words such as "violin" or "bass clarinet"'),
      family: z.string().optional().describe('Top-level family: strings, wind, brass, keyboard, voice, pluck, drum, metal, wood, pitched-percussion, …'),
      include_variants: z.boolean().optional()
        .describe('Include variants such as "Clarinet in A" (default false)'),
      limit: z.number().int().min(1).max(200).optional()
    }),
    annotations: readOnly
  }, async ({ query, family, include_variants, limit }) =>
    callFiddleTool('dorico.instruments.search',
      defined({ query, family, includeVariants: include_variants, limit })));

  server.registerTool('fiddle_list_plugins', {
    title: 'List plug-ins',
    description: 'List the plug-ins Fiddle knows from its last scan, with UIDs, whether each is an instrument, and whether it loaded (valid). scanning is true while a scan runs.',
    inputSchema: z.object({
      query: z.string().optional().describe('Words to match in the name or manufacturer'),
      instruments_only: z.boolean().optional(),
      limit: z.number().int().min(1).max(1000).optional()
    }),
    annotations: readOnly
  }, async ({ query, instruments_only, limit }) =>
    callFiddleTool('plugins.list',
      defined({ query, instrumentsOnly: instruments_only, limit })));

  server.registerTool('fiddle_scan_plugins', {
    title: 'Scan for plug-ins',
    description: 'Start a VST3 scan in the background, as the Scan button does, so Fiddle finds newly installed players. It returns at once; call fiddle_list_plugins until scanning is false. A full rescan re-probes every plug-in and is slow.',
    inputSchema: z.object({
      full_rescan: z.boolean().optional().describe('Re-probe every plug-in (default: only new or changed ones)')
    }),
    annotations: settable
  }, async ({ full_rescan }) =>
    callFiddleTool('plugins.scan', defined({ fullRescan: full_rescan })));

  server.registerTool('fiddle_list_maps', {
    title: 'List Fiddle maps',
    description: 'List the expression and percussion maps in Fiddle\'s collection, which any patch or layer can use, with how many patches and layers use each.',
    inputSchema: z.object({
      kind: z.enum(['expression', 'percussion']).optional(),
      query: z.string().optional().describe('Words to match in the name, ID, creator, player names or source file'),
      favorites_only: z.boolean().optional(),
      limit: z.number().int().min(1).max(1000).optional()
    }),
    annotations: readOnly
  }, async ({ kind, query, favorites_only, limit }) =>
    callFiddleTool('maps.list',
      defined({ kind, query, favoritesOnly: favorites_only, limit })));

  // ── Libraries ──────────────────────────────────────────────────────────

  server.registerTool('fiddle_list_libraries', {
    title: 'List libraries',
    description: 'List Fiddle\'s sample libraries with patch counts, patches still waiting for a preset capture, and how many layers use them.',
    annotations: readOnly
  }, async () => callFiddleTool('library.list'));

  server.registerTool('fiddle_get_library', {
    title: 'Get library',
    description: 'Return one library\'s patches: instrument, player plug-in, maps, Play as defaults, expected preset, whether the player state is captured, and how many layers use each.',
    inputSchema: z.object({ library_id: z.string().min(1) }),
    annotations: readOnly
  }, async ({ library_id }) =>
    callFiddleTool('library.get', { libraryId: library_id }));

  server.registerTool('fiddle_save_library', {
    title: 'Create or edit a library',
    description: 'Create a library (omit library_id) or change one: rename it, add patches, change patches, or remove patches that no layer uses. Patch fields you omit keep their values. Maps can come from Fiddle\'s collection or be imported from a .doricolib path. Patches may use different player plug-ins and need not have a map. A patch with expected_preset_name waits for the user to load that preset and capture it in Library Manager (fiddle_open_guided_library). One step on the library Undo history. Refused while Dorico plays or while Library Manager has unsaved edits. Present the plan to the user first.',
    inputSchema: z.object({
      library_id: z.string().min(1).optional().describe('Omit to create a new library'),
      name: z.string().min(1).optional().describe('Required for a new library'),
      vendor: z.string().optional(),
      variant: z.string().optional(),
      plugin_uid: z.number().int().optional()
        .describe('Default player plug-in for new patches'),
      patches: z.array(libraryPatch).max(256).optional(),
      remove_patch_ids: z.array(z.string().min(1)).max(256).optional()
    }),
    annotations: additive
  }, async ({ library_id, name, vendor, variant, plugin_uid, patches, remove_patch_ids }) =>
    callFiddleTool('library.save', defined({
      libraryId: library_id, name, vendor, variant, pluginUid: plugin_uid,
      removePatchIds: remove_patch_ids,
      patches: patches?.map(patch => defined({
        id: patch.id,
        name: patch.name,
        instrumentEntityId: patch.instrument_entity_id,
        family: patch.family,
        character: patch.character,
        pluginUid: patch.plugin_uid,
        expressionMapId: patch.expression_map_id,
        expressionMapPath: patch.expression_map_path,
        percussionMapId: patch.percussion_map_id,
        percussionMapPath: patch.percussion_map_path,
        percussionPlayAs: patch.percussion_play_as,
        expectedPresetName: patch.expected_preset_name
      }))
    })));

  server.registerTool('fiddle_delete_library', {
    title: 'Delete library',
    description: 'Delete a library and its patches. Refused while any layer uses one of its patches. Undo with fiddle_undo history "library".',
    inputSchema: z.object({ library_id: z.string().min(1) }),
    annotations: destructive
  }, async ({ library_id }) =>
    callFiddleTool('library.delete', { libraryId: library_id }));

  // ── Chairs, kits and layers ────────────────────────────────────────────

  server.registerTool('fiddle_get_chairs', {
    title: 'Get chairs',
    description: 'Return the chairs Dorico can address (including empty ones), each with its Dorico instrument, role, MIDI port and channel, kit, and layers, plus the percussion kits and whether Dorico\'s installed playback template is out of date (templateStatus.dirty).',
    annotations: readOnly
  }, async () => callFiddleTool('chairs.get'));

  server.registerTool('fiddle_create_chairs', {
    title: 'Create chairs',
    description: 'Add chairs, one per Dorico instrument destination, such as "Violin I" (section) or "Flute 1" (solo), as one Undo step. Names default to the Chairs window\'s ("Flute 1", "Flute 2"); roles default to section for strings and choir, solo otherwise. Give kit_id to add an instrument to an existing kit; use fiddle_create_kit for new kits. Changing chairs makes the playback template out of date.',
    inputSchema: z.object({
      chairs: z.array(z.object({
        instrument_entity_id: z.string().min(1),
        name: z.string().min(1).optional(),
        role: role.optional(),
        family: z.string().optional(),
        kit_id: z.string().min(1).optional()
      })).min(1).max(128)
    }),
    annotations: additive
  }, async ({ chairs }) =>
    callFiddleTool('chairs.create', {
      chairs: chairs.map(chair => defined({
        instrumentEntityId: chair.instrument_entity_id,
        name: chair.name,
        role: chair.role,
        family: chair.family,
        kitId: chair.kit_id
      }))
    }));

  server.registerTool('fiddle_update_chair', {
    title: 'Rename chair or change role',
    description: 'Rename a chair or switch it between solo and section. Its layers and MIDI assignment stay.',
    inputSchema: z.object({
      chair_id: z.string().min(1),
      name: z.string().min(1).optional(),
      role: role.optional()
    }),
    annotations: settable
  }, async ({ chair_id, name, role: newRole }) =>
    callFiddleTool('chairs.update', defined({ chairId: chair_id, name, role: newRole })));

  server.registerTool('fiddle_delete_chair', {
    title: 'Delete chair',
    description: 'Delete a chair and all of its layers. Confirm with the user first. Undo with fiddle_undo.',
    inputSchema: z.object({ chair_id: z.string().min(1) }),
    annotations: destructive
  }, async ({ chair_id }) =>
    callFiddleTool('chairs.delete', { chairId: chair_id }));

  server.registerTool('fiddle_create_kit', {
    title: 'Create percussion kit',
    description: 'Add a percussion kit: one solo chair per instrument, grouped under one name. Give from_dorico_kit (a Dorico kit instrument ID from fiddle_search_dorico_instruments) or a list of instruments.',
    inputSchema: z.object({
      name: z.string().min(1),
      family: z.string().optional(),
      from_dorico_kit: z.string().min(1).optional(),
      instruments: z.array(z.object({
        instrument_entity_id: z.string().min(1),
        name: z.string().min(1).optional()
      })).min(1).max(64).optional()
    }),
    annotations: additive
  }, async ({ name, family, from_dorico_kit, instruments }) =>
    callFiddleTool('kits.create', defined({
      name, family, fromDoricoKit: from_dorico_kit,
      instruments: instruments?.map(item => defined({
        instrumentEntityId: item.instrument_entity_id, name: item.name
      }))
    })));

  server.registerTool('fiddle_rename_kit', {
    title: 'Rename kit',
    description: 'Rename a percussion kit.',
    inputSchema: z.object({ kit_id: z.string().min(1), name: z.string().min(1) }),
    annotations: settable
  }, async ({ kit_id, name }) =>
    callFiddleTool('kits.rename', { kitId: kit_id, name }));

  server.registerTool('fiddle_delete_kit', {
    title: 'Delete kit',
    description: 'Delete a percussion kit\'s chairs and their layers. Confirm with the user first.',
    inputSchema: z.object({ kit_id: z.string().min(1) }),
    annotations: destructive
  }, async ({ kit_id }) =>
    callFiddleTool('kits.delete', { kitId: kit_id }));

  server.registerTool('fiddle_add_layer', {
    title: 'Add layer to chair',
    description: 'Add a library patch to a chair as a new layer with its own plug-in instance, initialized from the patch. A chair can have several layers. With whole_kit, also adds it to the chair\'s kit-mates that lack it. Layers don\'t change the playback template.',
    inputSchema: z.object({
      chair_id: z.string().min(1),
      patch_id: z.string().min(1).describe('Patch ID from fiddle_get_library'),
      whole_kit: z.boolean().optional()
    }),
    annotations: additive
  }, async ({ chair_id, patch_id, whole_kit }) =>
    callFiddleTool('layers.add', defined({ chairId: chair_id, patchId: patch_id, wholeKit: whole_kit })));

  server.registerTool('fiddle_remove_layer', {
    title: 'Remove layer',
    description: 'Remove a layer and its plug-in instance and mixer settings. With whole_kit, also its linked copies on the kit\'s other chairs. Undo with fiddle_undo.',
    inputSchema: z.object({
      layer_id: z.string().min(1),
      whole_kit: z.boolean().optional()
    }),
    annotations: destructive
  }, async ({ layer_id, whole_kit }) =>
    callFiddleTool('layers.remove', defined({ layerId: layer_id, wholeKit: whole_kit })));

  server.registerTool('fiddle_install_playback_template', {
    title: 'Install Dorico playback template',
    description: 'Write the Fiddle playback template for the current chairs into Dorico\'s user library, replacing the installed one. Ask the user first and have them quit Dorico. Afterwards Dorico must be restarted and the Fiddle template reapplied in open projects. Refused while Dorico plays.',
    annotations: destructive
  }, async () => callFiddleTool('template.install'));
}
