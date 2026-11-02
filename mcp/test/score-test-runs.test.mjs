import assert from 'node:assert/strict';
import { copyFile, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';

import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';

// The sample run record is written by ScoreTestTest --write-fixture: the
// basic-rhythm test with note 3 played 12 ms late and an extra note.
const fixture = join(import.meta.dirname, '..', '..', 'tests', 'score',
  'fixtures', 'sample-run.json');
const runId = 'basic-rhythm-20261021-143200';

test('score test runs are listed and explained from their records', async () => {
  const runs = await mkdtemp(join(tmpdir(), 'fiddle-score-runs-'));
  await copyFile(fixture, join(runs, `${runId}.json`));
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [join(import.meta.dirname, '..', 'dist', 'fiddle-mcp.mjs')],
    // No Fiddle is running: these tools only read run records.
    env: { ...process.env, FIDDLE_SCORE_TEST_RUNS_DIR: runs,
           FIDDLE_AGENT_CONTROL_FILE: join(runs, 'absent.json') }
  });
  const client = new Client({ name: 'score-test-runs-test', version: '1.0.0' });
  try {
    await client.connect(transport);
    const tools = (await client.listTools()).tools;
    for (const name of ['fiddle_list_score_test_runs', 'fiddle_get_score_test_run'])
      assert.equal(tools.find(tool => tool.name === name)?.annotations?.readOnlyHint, true);

    const listed = await client.callTool({ name: 'fiddle_list_score_test_runs',
                                           arguments: { test_id: 'basic-rhythm' } });
    assert.deepEqual(listed.structuredContent.runs.map(run => [run.run_id, run.pass]),
                     [[runId, false]]);
    const none = await client.callTool({ name: 'fiddle_list_score_test_runs',
                                         arguments: { test_id: 'slurs' } });
    assert.deepEqual(none.structuredContent.runs, []);

    const run = (await client.callTool({ name: 'fiddle_get_score_test_run',
                                         arguments: { run_id: runId } })).structuredContent;
    assert.equal(run.pass, false);
    assert.equal(run.summary.failed, 1);
    assert.equal(run.summary.extra, 1);
    assert.equal(run.failed_notes.length, 1);
    const late = run.failed_notes[0];
    assert.equal(late.index, 3);
    assert.equal(late.expected.onset_ms, 4800);
    assert.equal(late.received.onset_ms, 4812);
    assert.deepEqual(late.checks.map(check => check.check), ['onset', 'duration']);
    assert.equal(run.extras[0].name, 'F#6');
    assert.equal(run.bindings[0].bound_by, 'chair instrument');
    assert.equal(run.midi, undefined);

    const detail = (await client.callTool({ name: 'fiddle_get_score_test_run',
      arguments: { run_id: runId, note: 3, midi_window_ms: 10 } })).structuredContent;
    assert.ok(detail.note.checks.length > 2, 'all checks for the chosen note');
    assert.ok(detail.midi.some(e => e[0] === 4812 && e[3] === 'on' && e[4] === 77));
    assert.ok(detail.midi.every(e => Math.abs(e[0] - 4812) <= 10));

    for (const bad of ['../secret', 'no-such-run'])
      assert.equal((await client.callTool({ name: 'fiddle_get_score_test_run',
                                            arguments: { run_id: bad } })).isError, true);
  } finally {
    await client.close();
    await rm(runs, { recursive: true, force: true });
  }
});
