// fiddle_set_patch_preset_with_user against a mock Fiddle: the question reaches
// the user on both protocol eras, an answer captures or closes the player, and
// a client that can't ask gets instructions instead.
import assert from 'node:assert/strict';
import { unlink, writeFile } from 'node:fs/promises';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';

import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';

async function withFiddle(run, { replaceWhileLoading = false, captureRefused } = {}) {
  const descriptor = join(tmpdir(), `fiddle-preset-${process.pid}-${Math.random()}.json`);
  const token = 'preset-test-token';
  const calls = [];
  let statusCalls = 0;
  let refused = false;
  const backend = createServer(socket => {
    let input = '';
    socket.on('data', chunk => {
      input += chunk.toString('utf8');
      if (!input.includes('\n')) return;
      const request = JSON.parse(input.split('\n', 1)[0]);
      calls.push(request);
      const player = { patchId: request.params.patchId, open: true, session: 'S7', windowTitle: 'Synchron Pianos — Concert Grand' };
      // Loaded on the second look, so the adapter has to poll; or, to test a
      // window replaced meanwhile, another session by then.
      if (request.method === 'patch.player.status') ++statusCalls;
      // A refused capture: the window was closed meanwhile ('closed') or is
      // still open ('open').
      if (request.method === 'patch.player.capture' && captureRefused) {
        refused = true;
        socket.end(JSON.stringify({ id: request.id, ok: false, error: 'Capture refused' }) + '\n');
        return;
      }
      if (request.method === 'patch.player.status' && refused && captureRefused === 'closed') {
        socket.end(JSON.stringify({ id: request.id, ok: true, result: { patchId: request.params.patchId, open: false } }) + '\n');
        return;
      }
      const results = {
        'patch.player.open': { ...player, status: 'loading', expectedPresetName: 'Concert D-274' },
        'patch.player.status': statusCalls > 1
          ? { ...player, session: replaceWhileLoading ? 'S8' : 'S7', status: 'loaded' }
          : { ...player, status: 'loading' },
        'patch.player.capture': { captured: true, patchId: request.params.patchId },
        'patch.player.close': { closed: true }
      };
      socket.end(JSON.stringify({ id: request.id, ok: true, result: results[request.method] ?? {} }) + '\n');
    });
  });
  await new Promise(resolve => backend.listen(0, '127.0.0.1', resolve));
  await writeFile(descriptor, JSON.stringify({
    schemaVersion: 1, transport: 'tcp', host: '127.0.0.1',
    port: backend.address().port, token, pid: process.pid
  }));
  try {
    await run(descriptor, calls);
  } finally {
    await new Promise(resolve => backend.close(resolve));
    await unlink(descriptor);
  }
}

async function connect(descriptor, { mode, answer }) {
  const client = new Client({ name: 'preset-test', version: '1.0.0' }, {
    capabilities: answer ? { elicitation: {} } : {},
    versionNegotiation: { mode }
  });
  const asked = [];
  if (answer)
    client.setRequestHandler('elicitation/create', async request => {
      asked.push(request.params.message);
      return answer;
    });
  await client.connect(new StdioClientTransport({
    command: process.execPath,
    args: [join(import.meta.dirname, '..', 'dist', 'fiddle-mcp.mjs')],
    env: { ...process.env, FIDDLE_AGENT_CONTROL_FILE: descriptor }
  }));
  return { client, asked };
}

const methods = calls => calls.map(call => call.method);

for (const mode of ['legacy', 'auto']) {
  test(`${mode}: the user chooses the preset and it is captured`, () => withFiddle(async (descriptor, calls) => {
    const { client, asked } = await connect(descriptor, {
      mode, answer: { action: 'accept', content: { already_loaded: false } }
    });
    try {
      const result = await client.callTool({ name: 'fiddle_set_patch_preset_with_user', arguments: { patch_id: 'p1' } });
      assert.equal(result.isError, undefined, JSON.stringify(result));
      assert.deepEqual(result.structuredContent, { captured: true, patchId: 'p1' });
      assert.equal(asked.length, 1);
      assert.match(asked[0], /Concert D-274/);
      assert.match(asked[0], /Synchron Pianos — Concert Grand/);
      // Opened once, waited for loading, captured; never reopened on the retry.
      assert.equal(methods(calls).filter(m => m === 'patch.player.open').length, 1);
      // It polled until loaded before asking.
      assert.ok(methods(calls).filter(m => m === 'patch.player.status').length >= 2);
      const capture = calls.find(call => call.method === 'patch.player.capture');
      // The answer is tied to the window it was about (session S7), also
      // across the 2026-07-28 retry.
      assert.deepEqual(capture.params, { patchId: 'p1', session: 'S7', allowUnchanged: false });
    } finally {
      await client.close();
    }
  }));

  test(`${mode}: an already-loaded preset is captured unchanged`, () => withFiddle(async (descriptor, calls) => {
    const { client } = await connect(descriptor, {
      mode, answer: { action: 'accept', content: { already_loaded: true } }
    });
    try {
      await client.callTool({ name: 'fiddle_set_patch_preset_with_user', arguments: { patch_id: 'p1' } });
      assert.deepEqual(calls.find(call => call.method === 'patch.player.capture').params,
                       { patchId: 'p1', session: 'S7', allowUnchanged: true });
    } finally {
      await client.close();
    }
  }));

  test(`${mode}: declining skips the patch and closes the player`, () => withFiddle(async (descriptor, calls) => {
    const { client } = await connect(descriptor, { mode, answer: { action: 'decline' } });
    try {
      const result = await client.callTool({ name: 'fiddle_set_patch_preset_with_user', arguments: { patch_id: 'p1' } });
      assert.equal(result.structuredContent.captured, false);
      assert.match(result.structuredContent.reason, /skipped/);
      assert.deepEqual(calls.find(call => call.method === 'patch.player.close').params,
                       { patchId: 'p1', session: 'S7' });
      assert.ok(!methods(calls).includes('patch.player.capture'));
    } finally {
      await client.close();
    }
  }));
}

test('a window replaced while loading is never asked about or captured', () => withFiddle(async (descriptor, calls) => {
  const { client, asked } = await connect(descriptor, {
    mode: 'legacy', answer: { action: 'accept', content: {} }
  });
  try {
    const result = await client.callTool({ name: 'fiddle_set_patch_preset_with_user', arguments: { patch_id: 'p1' } });
    assert.equal(result.isError, true);
    assert.match(result.content[0].text, /closed or reopened/);
    assert.equal(asked.length, 0);
    assert.ok(!methods(calls).includes('patch.player.capture'));
  } finally {
    await client.close();
  }
}, { replaceWhileLoading: true }));

for (const where of ['open', 'closed'])
  test(`a refused capture says whether the player is still open (${where})`, () => withFiddle(async (descriptor) => {
    const { client } = await connect(descriptor, {
      mode: 'legacy', answer: { action: 'accept', content: {} }
    });
    try {
      const result = await client.callTool({ name: 'fiddle_set_patch_preset_with_user', arguments: { patch_id: 'p1' } });
      assert.equal(result.structuredContent.captured, false);
      assert.match(result.structuredContent.reason, /Capture refused/);
      assert.equal(result.structuredContent.playerStillOpen, where === 'open');
    } finally {
      await client.close();
    }
  }, { captureRefused: where }));

test('a client that cannot ask gets instructions and the player stays open', () => withFiddle(async (descriptor, calls) => {
  const { client } = await connect(descriptor, { mode: 'legacy' });
  try {
    const result = await client.callTool({ name: 'fiddle_set_patch_preset_with_user', arguments: { patch_id: 'p1' } });
    assert.equal(result.structuredContent.waitingForUser, true);
    assert.match(result.structuredContent.instructions, /fiddle_capture_patch_player/);
    assert.match(result.structuredContent.instructions, /Concert D-274/);
    assert.ok(!methods(calls).includes('patch.player.capture'));
    assert.ok(!methods(calls).includes('patch.player.close'));

    // The plain tools map their arguments.
    await client.callTool({ name: 'fiddle_capture_patch_player', arguments: { patch_id: 'p1', allow_unchanged: true } });
    assert.deepEqual(calls.at(-1).params, { patchId: 'p1', allowUnchanged: true });
    await client.callTool({ name: 'fiddle_open_patch_player', arguments: { patch_id: 'p1' } });
    assert.equal(calls.at(-1).method, 'patch.player.open');
    await client.callTool({ name: 'fiddle_get_patch_player', arguments: { patch_id: 'p1' } });
    assert.equal(calls.at(-1).method, 'patch.player.status');
    await client.callTool({ name: 'fiddle_close_patch_player', arguments: { patch_id: 'p1' } });
    assert.equal(calls.at(-1).method, 'patch.player.close');
  } finally {
    await client.close();
  }
}));
