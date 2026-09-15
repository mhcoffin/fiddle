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
  } finally {
    await client.close();
    await new Promise(resolve => backend.close(resolve));
    await unlink(descriptor);
  }
});
