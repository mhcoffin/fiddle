import { McpServer } from '@modelcontextprotocol/server';
import { serveStdio } from '@modelcontextprotocol/server/stdio';
import * as z from 'zod/v4';

import { callFiddle } from './fiddle-client.js';

type JsonObject = Record<string, unknown>;

function toolResult(value: unknown) {
  const structuredContent: JsonObject =
    value && typeof value === 'object' && !Array.isArray(value)
      ? value as JsonObject
      : { value };
  return {
    content: [{ type: 'text' as const,
                text: JSON.stringify(structuredContent, null, 2) }],
    structuredContent
  };
}

async function callFiddleTool(method: string,
                              params: Record<string, unknown> = {}) {
  try {
    return toolResult(await callFiddle(method, params));
  } catch (error) {
    return {
      content: [{ type: 'text' as const,
                  text: error instanceof Error ? error.message : String(error) }],
      isError: true
    };
  }
}

function createServer(): McpServer {
  const server = new McpServer(
    { name: 'fiddle', version: '0.1.0' },
    {
      instructions:
        'Inspect Fiddle before editing. Layer IDs come from fiddle_get_mixer. Library imports should begin with fiddle_inspect_library_sources; use map_query when the supplied directory contains multiple products. Present the proposed patches before fiddle_create_guided_library. Fiddle records edits in Undo. Do not change controls while Dorico is playing unless the user explicitly asks.'
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
    description: 'Read expression-map metadata under a local directory and find installed instrument plug-ins. Use this to prepare a guided library import without changing the catalog.',
    inputSchema: z.object({
      expression_map_directory: z.string().min(1)
        .describe('Local directory containing .doricolib files'),
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
  }, async ({ expression_map_directory, map_query, plugin_query, max_maps }) =>
    callFiddleTool('library.inspectSources', {
      expressionMapDirectory: expression_map_directory,
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

  server.registerTool('fiddle_undo', {
    title: 'Undo Fiddle change',
    description: 'Undo the most recent change in Fiddle and return the resulting mixer state.',
    annotations: {
      readOnlyHint: false, destructiveHint: false, idempotentHint: false,
      openWorldHint: false
    }
  }, async () => callFiddleTool('history.undo'));

  server.registerTool('fiddle_redo', {
    title: 'Redo Fiddle change',
    description: 'Redo the next change in Fiddle history and return the resulting mixer state.',
    annotations: {
      readOnlyHint: false, destructiveHint: false, idempotentHint: false,
      openWorldHint: false
    }
  }, async () => callFiddleTool('history.redo'));

  return server;
}

await serveStdio(() => createServer());
