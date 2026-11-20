import assert from 'node:assert/strict';
import { writeFile, unlink } from 'node:fs/promises';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';

import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';

test('MCP adapter advertises tools and calls the Fiddle bridge', async () => {
  const descriptor = join(tmpdir(), `fiddle-mcp-e2e-${process.pid}.json`);
  const token = 'mcp-test-token';
  const requests = [];
  const backend = createServer(socket => {
    let input = '';
    socket.on('data', chunk => {
      input += chunk.toString('utf8');
      if (!input.includes('\n')) return;
      const request = JSON.parse(input.split('\n', 1)[0]);
      assert.equal(request.token, token);
      requests.push(request);
      const result = request.method === 'status'
        ? { ready: true, transportPlaying: false }
        : {};
      socket.end(JSON.stringify({ id: request.id, ok: true, result }) + '\n');
    });
  });
  await new Promise(resolve => backend.listen(0, '127.0.0.1', resolve));
  const address = backend.address();
  assert.equal(typeof address, 'object');
  await writeFile(descriptor, JSON.stringify({
    schemaVersion: 1, transport: 'tcp', host: '127.0.0.1',
    port: address.port, token, pid: process.pid
  }));

  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [join(import.meta.dirname, '..', 'dist', 'fiddle-mcp.mjs')],
    env: { ...process.env, FIDDLE_AGENT_CONTROL_FILE: descriptor }
  });
  const client = new Client({ name: 'fiddle-mcp-test', version: '1.0.0' });
  try {
    await client.connect(transport);
    const listed = await client.listTools();
    const statusTool = listed.tools.find(tool =>
      tool.name === 'fiddle_get_status');
    const gainTool = listed.tools.find(tool =>
      tool.name === 'fiddle_set_layer_gain');
    const undoTool = listed.tools.find(tool => tool.name === 'fiddle_undo');
    const inspectTool = listed.tools.find(tool =>
      tool.name === 'fiddle_inspect_library_sources');
    const createLibraryTool = listed.tools.find(tool =>
      tool.name === 'fiddle_create_guided_library');
    const openLibraryTool = listed.tools.find(tool =>
      tool.name === 'fiddle_open_guided_library');
    const setupTool = listed.tools.find(tool =>
      tool.name === 'fiddle_get_library_setup');
    assert.equal(statusTool?.annotations?.readOnlyHint, true);
    assert.equal(gainTool?.annotations?.readOnlyHint, false);
    assert.equal(gainTool?.annotations?.destructiveHint, false);
    assert.equal(gainTool?.annotations?.idempotentHint, true);
    assert.equal(undoTool?.annotations?.idempotentHint, false);
    assert.equal(inspectTool?.annotations?.readOnlyHint, true);
    assert.equal(createLibraryTool?.annotations?.readOnlyHint, false);
    assert.equal(createLibraryTool?.annotations?.idempotentHint, false);
    assert.equal(setupTool?.annotations?.readOnlyHint, true);
    assert.equal(openLibraryTool?.annotations?.idempotentHint, true);
    const status = await client.callTool({
      name: 'fiddle_get_status', arguments: {}
    });
    assert.deepEqual(status.structuredContent,
                     { ready: true, transportPlaying: false });

    await client.callTool({
      name: 'fiddle_inspect_library_sources',
      arguments: {
        expression_map_directory: '/tmp/maps',
        map_query: 'BBC Symphony Orchestra Discovery',
        plugin_query: 'BBC',
        max_maps: 25
      }
    });
    assert.deepEqual(requests.at(-1)?.params, {
      expressionMapDirectory: '/tmp/maps',
      mapQuery: 'BBC Symphony Orchestra Discovery',
      pluginQuery: 'BBC',
      maxMaps: 25
    });

    await client.callTool({
      name: 'fiddle_create_guided_library',
      arguments: {
        name: 'BBCSO Discover',
        vendor: 'Spitfire Audio',
        variant: 'Discover',
        plugin_uid: 1234,
        patches: [{
          id: 'violin-1',
          name: 'Violin 1',
          instrument_entity_id: 'strings.violin',
          family: 'Strings',
          character: 'section',
          expression_map_id: 'map-1',
          expression_map_path: '/tmp/maps/bbcso.doricolib',
          expected_preset_name: 'BBCSO Discover Violin 1'
        }]
      }
    });
    assert.equal(requests.at(-1)?.method, 'library.setup.create');
    assert.deepEqual(requests.at(-1)?.params, {
      name: 'BBCSO Discover',
      vendor: 'Spitfire Audio',
      variant: 'Discover',
      pluginUid: 1234,
      patches: [{
        id: 'violin-1',
        name: 'Violin 1',
        instrumentEntityId: 'strings.violin',
        family: 'Strings',
        character: 'section',
        expressionMapId: 'map-1',
        expressionMapPath: '/tmp/maps/bbcso.doricolib',
        expectedPresetName: 'BBCSO Discover Violin 1'
      }]
    });

    // Setup tools: annotations and the snake_case -> camelCase mapping.
    const tool = name => listed.tools.find(item => item.name === name);
    for (const name of ['fiddle_search_dorico_instruments', 'fiddle_list_plugins',
      'fiddle_find_patches',
      'fiddle_list_maps', 'fiddle_list_libraries', 'fiddle_get_library',
      'fiddle_get_chairs', 'fiddle_get_bridge_maps'])
      assert.equal(tool(name)?.annotations?.readOnlyHint, true, name);
    for (const name of ['fiddle_delete_chair', 'fiddle_delete_kit',
      'fiddle_delete_library', 'fiddle_remove_layer',
      'fiddle_install_playback_template', 'fiddle_update_layers_from_library'])
      assert.equal(tool(name)?.annotations?.destructiveHint, true, name);
    for (const name of ['fiddle_save_library', 'fiddle_create_chairs',
      'fiddle_create_kit', 'fiddle_add_layer', 'fiddle_scan_plugins',
      'fiddle_update_chair', 'fiddle_rename_kit'])
      assert.equal(tool(name)?.annotations?.readOnlyHint, false, name);

    const call = async (name, args, method, params) => {
      await client.callTool({ name, arguments: args });
      assert.equal(requests.at(-1)?.method, method);
      assert.deepEqual(requests.at(-1)?.params, params);
    };
    await call('fiddle_inspect_library_sources', { kind: 'percussion' },
      'library.inspectSources', { kind: 'percussion' });
    await call('fiddle_search_dorico_instruments',
      { query: 'violin', include_variants: true, limit: 5 },
      'dorico.instruments.search', { query: 'violin', includeVariants: true, limit: 5 });
    await call('fiddle_list_plugins', { query: 'BBC', instruments_only: true },
      'plugins.list', { query: 'BBC', instrumentsOnly: true });
    await call('fiddle_scan_plugins', {}, 'plugins.scan', {});
    await call('fiddle_list_maps', { kind: 'expression', favorites_only: true },
      'maps.list', { kind: 'expression', favoritesOnly: true });
    await call('fiddle_list_maps', { instrument_entity_id: 'instrument.drum.hihat' },
      'maps.list', { instrumentEntityId: 'instrument.drum.hihat' });
    await call('fiddle_find_patches',
      { query: 'flute', instrument_entity_id: 'instrument.wind.flutes.flute', plugin_uid: 3, limit: 9 },
      'patches.find',
      { query: 'flute', instrumentEntityId: 'instrument.wind.flutes.flute', pluginUid: 3, limit: 9 });
    await call('fiddle_find_patches', { chair_id: 'c' }, 'patches.find', { chairId: 'c' });
    await call('fiddle_get_mixer', {}, 'mixer.get', {});
    await call('fiddle_get_mixer', { query: 'flute', chair_id: 'c', layer_id: 'l' },
      'mixer.get', { query: 'flute', chairId: 'c', layerId: 'l' });
    await call('fiddle_list_libraries', {}, 'library.list', {});
    await call('fiddle_get_library', { library_id: 'lib' }, 'library.get', { libraryId: 'lib' });
    await call('fiddle_save_library', {
      library_id: 'lib',
      plugin_uid: 7,
      remove_patch_ids: ['old'],
      patches: [
        { id: 'p1', expression_map_id: '' },
        { name: 'Cello', instrument_entity_id: 'instrument.strings.cello',
          character: 'section', percussion_play_as: { a: 'b' },
          expression_map_id: 'm', expression_map_path: '/m.doricolib',
          expected_preset_name: 'Cellos' }
      ]
    }, 'library.save', {
      libraryId: 'lib',
      pluginUid: 7,
      removePatchIds: ['old'],
      patches: [
        { id: 'p1', expressionMapId: '' },
        { name: 'Cello', instrumentEntityId: 'instrument.strings.cello',
          character: 'section', percussionPlayAs: { a: 'b' },
          expressionMapId: 'm', expressionMapPath: '/m.doricolib',
          expectedPresetName: 'Cellos' }
      ]
    });
    await call('fiddle_delete_library', { library_id: 'lib' }, 'library.delete', { libraryId: 'lib' });
    await call('fiddle_get_chairs', {}, 'chairs.get', {});
    await call('fiddle_get_chairs', { query: 'flute', chair_id: 'c', without_layers: true },
      'chairs.get', { query: 'flute', chairId: 'c', withoutLayers: true });
    await call('fiddle_create_chairs', {
      chairs: [
        { instrument_entity_id: 'instrument.strings.violin', name: 'Violin I', role: 'section' },
        { instrument_entity_id: 'instrument.wind.flutes.flute' },
        { instrument_entity_id: 'instrument.drum.snare', kit_id: 'kit' }
      ]
    }, 'chairs.create', {
      chairs: [
        { instrumentEntityId: 'instrument.strings.violin', name: 'Violin I', role: 'section' },
        { instrumentEntityId: 'instrument.wind.flutes.flute' },
        { instrumentEntityId: 'instrument.drum.snare', kitId: 'kit' }
      ]
    });
    await call('fiddle_update_chair', { chair_id: 'c', role: 'solo' },
      'chairs.update', { chairId: 'c', role: 'solo' });
    await call('fiddle_delete_chair', { chair_id: 'c' }, 'chairs.delete', { chairId: 'c' });
    await call('fiddle_create_kit', { name: 'Blocks', from_dorico_kit: 'instrument.kit.blocks' },
      'kits.create', { name: 'Blocks', fromDoricoKit: 'instrument.kit.blocks' });
    await call('fiddle_create_kit',
      { name: 'Drums', instruments: [{ instrument_entity_id: 'a' }, { instrument_entity_id: 'b', name: 'B' }] },
      'kits.create',
      { name: 'Drums', instruments: [{ instrumentEntityId: 'a' }, { instrumentEntityId: 'b', name: 'B' }] });
    await call('fiddle_rename_kit', { kit_id: 'k', name: 'N' }, 'kits.rename', { kitId: 'k', name: 'N' });
    await call('fiddle_delete_kit', { kit_id: 'k' }, 'kits.delete', { kitId: 'k' });
    await call('fiddle_add_layer', { chair_id: 'c', patch_id: 'p', whole_kit: true },
      'layers.add', { chairId: 'c', patchId: 'p', wholeKit: true });
    await call('fiddle_remove_layer', { layer_id: 'l' }, 'layers.remove', { layerId: 'l' });
    await call('fiddle_install_playback_template', {}, 'template.install', {});
    await call('fiddle_update_layers_from_library', { layer_id: 'l' }, 'layers.refresh', { layerId: 'l' });
    await call('fiddle_update_layers_from_library', { patch_id: 'p' }, 'layers.refresh', { patchId: 'p' });
    await call('fiddle_get_bridge_maps', {}, 'bridge.get', {});
    await call('fiddle_update_bridge_maps', {}, 'bridge.update', {});
    await call('fiddle_update_bridge_maps', { section: 'strings' }, 'bridge.update', { section: 'strings' });
    // Exactly one of layer_id and patch_id.
    for (const args of [{}, { layer_id: 'l', patch_id: 'p' }]) {
      const before = requests.length;
      const result = await client.callTool({ name: 'fiddle_update_layers_from_library', arguments: args });
      assert.equal(result.isError, true, JSON.stringify(args));
      assert.equal(requests.length, before);
    }
    await call('fiddle_undo', {}, 'history.undo', {});
    await call('fiddle_undo', { history: 'library' }, 'history.undo', { history: 'library' });
    await call('fiddle_redo', { history: 'project' }, 'history.redo', { history: 'project' });
  } finally {
    await client.close();
    await new Promise(resolve => backend.close(resolve));
    await unlink(descriptor);
  }
});
