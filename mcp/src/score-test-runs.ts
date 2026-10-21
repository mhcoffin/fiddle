// Score test run records (tests/score/README.md), read straight from disk so
// they can be looked up whether or not Fiddle is running.
import { readdir, readFile } from 'node:fs/promises';
import { homedir } from 'node:os';
import { join } from 'node:path';

type JsonObject = Record<string, any>;

export function runsFolder(environment = process.env): string {
  if (environment.FIDDLE_SCORE_TEST_RUNS_DIR)
    return environment.FIDDLE_SCORE_TEST_RUNS_DIR;
  return join(homedir(), 'Library', 'Application Support', 'Fiddle',
    'ScoreTests', 'runs');
}

const runIdPattern = /^[A-Za-z0-9][A-Za-z0-9._-]*$/;

async function readRecord(path: string): Promise<JsonObject | undefined> {
  try {
    return JSON.parse(await readFile(path, 'utf8'));
  } catch {
    return undefined;
  }
}

export async function listRuns(options: { testId?: string; limit?: number } = {}) {
  const folder = runsFolder();
  let names: string[] = [];
  try {
    names = (await readdir(folder)).filter(name => name.endsWith('.json'));
  } catch {
    return { runs_folder: folder, runs: [] };
  }
  const runs = [];
  for (const name of names) {
    const record = await readRecord(join(folder, name));
    if (!record || (options.testId && record.test_id !== options.testId))
      continue;
    runs.push({
      run_id: record.run_id, test_id: record.test_id, title: record.title,
      pass: record.pass, summary: record.summary,
      finished_at: record.finished_at
    });
  }
  runs.sort((a, b) => String(b.finished_at).localeCompare(String(a.finished_at)));
  return { runs_folder: folder, runs: runs.slice(0, options.limit ?? 20) };
}

// The run's result and everything needed to explain its failures: failed,
// missing and extra notes with what was expected and what arrived, and, for
// one note, the MIDI Fiddle saw around it (times relative to the score).
export async function getRun(runId: string,
                             options: { note?: number; midiWindowMs?: number; limit?: number } = {}) {
  if (!runIdPattern.test(runId))
    throw new Error(`Not a run ID: ${runId}`);
  const path = join(runsFolder(), `${runId}.json`);
  const record = await readRecord(path);
  if (!record)
    throw new Error(`No score test run ${runId} in ${runsFolder()}. List runs with fiddle_list_score_test_runs.`);
  const limit = options.limit ?? 50;
  const comparison = record.comparison ?? {};
  const expected = new Map<number, JsonObject>(
    (record.expected?.notes ?? []).map((n: JsonObject) => [n.index, n]));
  const withExpected = (note: JsonObject) => {
    const e = expected.get(note.index) ?? {};
    return {
      ...note,
      expected: {
        onset_ms: e.onset_ms, duration_ms: e.duration_ms,
        dynamic: e.dynamic, techniques: e.techniques
      },
      checks: (note.checks ?? []).filter((c: JsonObject) => !c.ok)
    };
  };
  const failed = (comparison.notes ?? []).filter((n: JsonObject) => n.status !== 'pass');
  const result: JsonObject = {
    record_path: path,
    run_id: record.run_id, test_id: record.test_id, title: record.title,
    pass: record.pass, summary: record.summary, failures: record.failures,
    finished_at: record.finished_at, sample_rate: record.sample_rate,
    discarded_takes: record.discarded_takes,
    dropped_midi_events: record.dropped_midi_events,
    offset_ms: comparison.offset_ms, tolerances: comparison.tolerances,
    bindings: comparison.bindings, bridges: record.context?.bridges ?? [],
    failed_notes: failed.slice(0, limit).map(withExpected),
    more_failed_notes: Math.max(0, failed.length - limit),
    extras: (comparison.extras ?? []).slice(0, limit)
  };
  if (options.note !== undefined) {
    const note = (comparison.notes ?? []).find((n: JsonObject) => n.index === options.note);
    if (!note)
      throw new Error(`Test ${record.test_id} has no note #${options.note}`);
    // MIDI times are from the first recorded event (origin_ms on the host
    // clock); comparison times subtract offset_ms.
    const shift = (comparison.offset_ms ?? 0) - (record.origin_ms ?? 0);
    const centre = (note.received?.onset_ms ?? expected.get(note.index)?.onset_ms ?? 0) + shift;
    const window = options.midiWindowMs ?? 50;
    result.note = withExpected(note);
    result.note.checks = note.checks;
    result.midi_columns = ['ms', 'port', 'channel', 'kind', 'a', 'b'];
    result.midi = (record.midi ?? [])
      .filter((e: any[]) => Math.abs(e[0] - centre) <= window)
      .map((e: any[]) => [Math.round((e[0] - shift) * 1000) / 1000, ...e.slice(1)]);
  }
  return result;
}
