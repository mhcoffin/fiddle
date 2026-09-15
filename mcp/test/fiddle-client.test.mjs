import assert from 'node:assert/strict';
import { unlink, writeFile } from 'node:fs/promises';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';

import { callFiddle, descriptorPath } from '../dist/fiddle-client.mjs';

test('descriptor path can be overridden for isolated clients', () => {
  assert.equal(descriptorPath({ FIDDLE_AGENT_CONTROL_FILE: '/tmp/test.json' }),
               '/tmp/test.json');
});

test('client authenticates and returns a successful result', async () => {
  const descriptor = join(tmpdir(), `fiddle-agent-${process.pid}.json`);
  const token = 'test-token';
  const server = createServer(socket => {
    let input = '';
    socket.on('data', chunk => {
      input += chunk.toString('utf8');
      if (!input.includes('\n')) return;
      const request = JSON.parse(input.split('\n', 1)[0]);
      assert.equal(request.token, token);
      assert.equal(request.method, 'status');
      socket.end(JSON.stringify({ id: request.id, ok: true,
                                  result: { ready: true } }) + '\n');
    });
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address();
  assert.equal(typeof address, 'object');
  await writeFile(descriptor, JSON.stringify({
    schemaVersion: 1, transport: 'tcp', host: '127.0.0.1',
    port: address.port, token, pid: process.pid
  }));
  const oldPath = process.env.FIDDLE_AGENT_CONTROL_FILE;
  process.env.FIDDLE_AGENT_CONTROL_FILE = descriptor;
  try {
    assert.deepEqual(await callFiddle('status'), { ready: true });
  } finally {
    if (oldPath === undefined) delete process.env.FIDDLE_AGENT_CONTROL_FILE;
    else process.env.FIDDLE_AGENT_CONTROL_FILE = oldPath;
    await new Promise(resolve => server.close(resolve));
    await unlink(descriptor);
  }
});
