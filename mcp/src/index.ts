import { McpServer } from '@modelcontextprotocol/server';
import { serveStdio } from '@modelcontextprotocol/server/stdio';
import * as z from 'zod/v4';

import { registerSetupTools } from './setup-tools.js';
import { callFiddleTool, localTool } from './tool-results.js';
import { getRun, listRuns } from './score-test-runs.js';

function createServer(): McpServer {
  const server = new McpServer(
    { name: 'fiddle', version: '0.1.0' },
    {
      instructions:
        'Inspect Fiddle before editing. Setup runs in this order: make sure the player plug-in is known (fiddle_list_plugins; fiddle_scan_plugins after the user installs one), build a library of patches (fiddle_save_library, or fiddle_create_guided_library when the user must pick presets in the player), create chairs for the instruments Dorico should play (fiddle_search_dorico_instruments for IDs, then fiddle_create_chairs or fiddle_create_kit), give each chair layers (fiddle_add_layer), and finally install the playback template (fiddle_install_playback_template). Read existing state first (fiddle_get_chairs, fiddle_list_libraries, fiddle_list_maps) so you extend rather than duplicate it. Present a plan to the user before creating libraries or several chairs, and before installing the template. Chair, kit and layer edits are on the project Undo history; library and map edits are on the library history (fiddle_undo with history "library"). Layer IDs come from fiddle_get_mixer or fiddle_get_chairs. Do not change anything while Dorico is playing unless the user explicitly asks; setup tools, Undo and Redo refuse during playback. Score test run IDs (from the Score Tests window) are explained by fiddle_get_score_test_run.'
    }
  );

  server.registerTool('fiddle_get_status', {
    title: 'Get Fiddle status',
    description: 'Report whether Fiddle is ready, playing, dirty, or detached, plus its current Undo/Redo state.',
    annotations: {
      readOnlyHint: true, destructiveHint: false, idempotentHint: true,
      openWorldHint: false
    }
  }, async () => callFiddleTool('status'));

  server.registerTool('fiddle_get_mixer', {
    title: 'Get Fiddle mixer',
    description: 'Return the current Fiddle layers, group buses, master state, project identity, and Undo/Redo state. Use layer IDs from this result for edits.',
    annotations: {
      readOnlyHint: true, destructiveHint: false, idempotentHint: true,
      openWorldHint: false
    }
  }, async () => callFiddleTool('mixer.get'));

  server.registerTool('fiddle_inspect_library_sources', {
    title: 'Inspect library setup sources',
    description: 'Read expression- or percussion-map metadata from .doricolib files and find installed instrument plug-ins, without changing the catalog. Without a directory it searches the folders Dorico loads libraries from. Each map says whether Fiddle already has it (inFiddle).',
    inputSchema: z.object({
      expression_map_directory: z.string().min(1).optional()
        .describe('Local directory containing .doricolib files; omit to search Dorico\'s library folders'),
      kind: z.enum(['expression', 'percussion']).optional()
        .describe('Which maps to list (default expression)'),
      map_query: z.string().optional()
        .describe('Case-insensitive path filter for expression maps, such as "BBC Symphony Orchestra Discovery"'),
      plugin_query: z.string().optional()
        .describe('Optional plug-in or manufacturer name filter'),
      max_maps: z.number().int().min(1).max(500).optional()
    }),
    annotations: {
      readOnlyHint: true, destructiveHint: false, idempotentHint: true,
      openWorldHint: false
    }
  }, async ({ expression_map_directory, kind, map_query, plugin_query, max_maps }) =>
    callFiddleTool('library.inspectSources', {
      ...(expression_map_directory ? { expressionMapDirectory: expression_map_directory } : {}),
      ...(kind ? { kind } : {}),
      ...(map_query ? { mapQuery: map_query } : {}),
      ...(plugin_query ? { pluginQuery: plugin_query } : {}),
      ...(max_maps ? { maxMaps: max_maps } : {})
    }));

  const guidedPatchSchema = z.object({
    id: z.string().min(1).optional(),
    name: z.string().min(1),
    instrument_entity_id: z.string().optional()
      .describe('Canonical Dorico instrument ID; omit when ambiguous'),
    family: z.string().optional(),
    character: z.enum(['solo', 'section', 'ensemble', 'overlay']).optional(),
    expression_map_id: z.string().min(1),
    expression_map_path: z.string().min(1),
    expected_preset_name: z.string().min(1)
      .describe('Exact preset name to show in the guided setup UI')
  });

  server.registerTool('fiddle_create_guided_library', {
    title: 'Create guided Fiddle library',
    description: 'Create a durable incomplete library with its players and expression maps assigned. Present the complete plan to the user before calling this tool; the user finishes each preset in Library Manager.',
    inputSchema: z.object({
      name: z.string().min(1),
      vendor: z.string().optional(),
      variant: z.string().optional(),
      plugin_uid: z.number().int(),
      patches: z.array(guidedPatchSchema).min(1).max(256)
    }),
    annotations: {
      readOnlyHint: false, destructiveHint: false, idempotentHint: false,
      openWorldHint: false
    }
  }, async ({ name, vendor, variant, plugin_uid, patches }) =>
    callFiddleTool('library.setup.create', {
      name, vendor: vendor ?? '', variant: variant ?? '', pluginUid: plugin_uid,
      patches: patches.map(patch => ({
        ...(patch.id ? { id: patch.id } : {}),
        name: patch.name,
        instrumentEntityId: patch.instrument_entity_id ?? '',
        family: patch.family ?? '',
        character: patch.character ?? '',
        expressionMapId: patch.expression_map_id,
        expressionMapPath: patch.expression_map_path,
        expectedPresetName: patch.expected_preset_name
      }))
    }));

  server.registerTool('fiddle_get_library_setup', {
    title: 'Get guided library progress',
    description: 'Return every patch and the configured/remaining counts for a guided library.',
    inputSchema: z.object({ library_id: z.string().min(1) }),
    annotations: {
      readOnlyHint: true, destructiveHint: false, idempotentHint: true,
      openWorldHint: false
    }
  }, async ({ library_id }) =>
    callFiddleTool('library.setup.get', { libraryId: library_id }));

  server.registerTool('fiddle_open_guided_library', {
    title: 'Open guided library setup',
    description: 'Bring Library Manager forward and open a saved library at its first unfinished player preset.',
    inputSchema: z.object({ library_id: z.string().min(1) }),
    annotations: {
      readOnlyHint: false, destructiveHint: false, idempotentHint: true,
      openWorldHint: false
    }
  }, async ({ library_id }) =>
    callFiddleTool('library.setup.open', { libraryId: library_id }));

  server.registerTool('fiddle_set_layer_gain', {
    title: 'Set layer gain',
    description: 'Set one Fiddle layer gain in dB. The change is recorded in Fiddle Undo.',
    inputSchema: z.object({
      strip_id: z.string().min(1).describe('Stable layer ID from fiddle_get_mixer'),
      gain_db: z.number().min(-120).max(6).describe('Gain in decibels')
    }),
    annotations: {
      readOnlyHint: false, destructiveHint: false, idempotentHint: true,
      openWorldHint: false
    }
  }, async ({ strip_id, gain_db }) =>
    callFiddleTool('layer.setGain',
      { stripId: strip_id, gainDb: gain_db }));

  server.registerTool('fiddle_set_layer_mute', {
    title: 'Set layer mute',
    description: 'Mute or unmute one Fiddle layer. The change is recorded in Fiddle Undo.',
    inputSchema: z.object({
      strip_id: z.string().min(1).describe('Stable layer ID from fiddle_get_mixer'),
      muted: z.boolean()
    }),
    annotations: {
      readOnlyHint: false, destructiveHint: false, idempotentHint: true,
      openWorldHint: false
    }
  }, async ({ strip_id, muted }) =>
    callFiddleTool('layer.setMute', { stripId: strip_id, muted }));

  server.registerTool('fiddle_set_layer_solo', {
    title: 'Set layer solo',
    description: 'Solo or unsolo one Fiddle layer. The change is recorded in Fiddle Undo.',
    inputSchema: z.object({
      strip_id: z.string().min(1).describe('Stable layer ID from fiddle_get_mixer'),
      soloed: z.boolean()
    }),
    annotations: {
      readOnlyHint: false, destructiveHint: false, idempotentHint: true,
      openWorldHint: false
    }
  }, async ({ strip_id, soloed }) =>
    callFiddleTool('layer.setSolo', { stripId: strip_id, soloed }));

  server.registerTool('fiddle_list_score_test_runs', {
    title: 'List score test runs',
    description: 'List recent score test runs (newest first) with pass/fail and note counts. Run records are read from disk; Fiddle need not be running.',
    inputSchema: z.object({
      test_id: z.string().optional().describe('Only runs of this test, such as "basic-rhythm"'),
      limit: z.number().int().min(1).max(200).optional()
    }),
    annotations: {
      readOnlyHint: true, destructiveHint: false, idempotentHint: true,
      openWorldHint: false
    }
  }, async ({ test_id, limit }) =>
    localTool(() => listRuns({ testId: test_id, limit })));

  server.registerTool('fiddle_get_score_test_run', {
    title: 'Get score test run',
    description: 'Explain a score test run by its run ID (shown in Fiddle\'s Score Tests window, for example basic-rhythm-20261021-143200): result, part bindings, bridge state, and every failed, missing and extra note with what was expected and what arrived. Pass note to also get that note\'s checks and the MIDI Fiddle saw around it.',
    inputSchema: z.object({
      run_id: z.string().min(1).describe('Run ID from the Score Tests window'),
      note: z.number().int().min(0).optional().describe('Expected note index to show in detail, with MIDI around it'),
      midi_window_ms: z.number().min(1).max(5000).optional().describe('MIDI window around the note, in ms (default 50)'),
      limit: z.number().int().min(1).max(500).optional().describe('Most failed and extra notes to return (default 50)')
    }),
    annotations: {
      readOnlyHint: true, destructiveHint: false, idempotentHint: true,
      openWorldHint: false
    }
  }, async ({ run_id, note, midi_window_ms, limit }) =>
    localTool(() => getRun(run_id, { note, midiWindowMs: midi_window_ms, limit })));

  const historySchema = z.object({
    history: z.enum(['project', 'library']).optional()
      .describe('project (default): mixer, chair, kit and layer edits. library: library and map catalog edits.')
  });

  server.registerTool('fiddle_undo', {
    title: 'Undo Fiddle change',
    description: 'Undo the most recent change in one of Fiddle\'s two histories. The project history holds mixer, chair, kit and layer edits; the library history holds library and map edits, including ones made by fiddle_save_library. fiddle_get_status shows both. Refused while Dorico plays.',
    inputSchema: historySchema,
    annotations: {
      readOnlyHint: false, destructiveHint: false, idempotentHint: false,
      openWorldHint: false
    }
  }, async ({ history }) => callFiddleTool('history.undo', history ? { history } : {}));

  server.registerTool('fiddle_redo', {
    title: 'Redo Fiddle change',
    description: 'Redo the next change in one of Fiddle\'s two histories (see fiddle_undo). Refused while Dorico plays.',
    inputSchema: historySchema,
    annotations: {
      readOnlyHint: false, destructiveHint: false, idempotentHint: false,
      openWorldHint: false
    }
  }, async ({ history }) => callFiddleTool('history.redo', history ? { history } : {}));

  registerSetupTools(server);

  return server;
}

await serveStdio(() => createServer());
