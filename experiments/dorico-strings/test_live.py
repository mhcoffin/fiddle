import copy
import json
from pathlib import Path
import tempfile
import unittest
import generate
import inspect_live


class LiveTest(unittest.TestCase):
    def rows(self):
        return [{'kind': 'header', 'schema': 1, 'capture_only': True, 'tie_policy': 'parameters-before-events'},
                {'kind': 'block', 'block': 0, 'inputs': 1, 'input_dropped': 0, 'input_invalid': 0,
                 'playing': True, 'host_samples': 0, 'has_context': True},
                {'kind': 'input', 'block': 0, 'sequence': 0, 'source': 1, 'offset': 0,
                 'host_samples': 0, 'port': 0, 'channel': 1, 'type': 0, 'data1': 72, 'data2': 90, 'valid': True},
                {'kind': 'end', 'blocks': 1, 'capture_dropped': 0}]

    def test_closed_complete_trace_required(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.jsonl'
            rows = self.rows()
            path.write_text('\n'.join(map(json.dumps, rows)))
            self.assertEqual(len(inspect_live.read_capture(path)[1]), 1)
            bad_rows = [rows[:-1], rows[:2] + rows[3:]]
            for field in ('capture_dropped', 'blocks'):
                altered = copy.deepcopy(rows)
                altered[-1][field] += 1
                bad_rows.append(altered)
            for field in ('input_dropped', 'input_invalid'):
                altered = copy.deepcopy(rows)
                altered[1][field] = 1
                bad_rows.append(altered)
            for altered in bad_rows:
                path.write_text('\n'.join(map(json.dumps, altered)))
                with self.assertRaises(ValueError):
                    inspect_live.read_capture(path)

    def test_all_notes_and_scalar_states_through_live_schema(self):
        decoder = json.loads(generate.render(generate.load_spec(), 'high-vibrato')[1])
        expected = json.loads((generate.HERE / 'expected.json').read_text())
        events = []
        for i, row in enumerate(expected):
            desired = set(row['techniques'])
            offset = 100 * i
            def add(kind, p1, p2, source):
                events.append({'block': 0, 'sequence': len(events), 'source': source, 'offset': offset,
                               'host_samples': offset, 'port': 0, 'channel': 1, 'type': kind,
                               'data1': p1, 'data2': p2, 'raw_value': p2 / 127})
            for cc, table in decoder['tables'].items():
                owned = {pt for v in table['values'].values() for pt in v['techniques']}
                value = next(int(v) for v, state in table['values'].items() if
                             set(state['techniques']) == desired & owned and state.get('length', 'kMedium') == 'kMedium')
                add(3, int(cc), value, 0)
            for key in decoder['key_switches']:
                prefix = 'on' if key['technique'] in desired else 'off'
                add(0, key[prefix + '_note'], key[prefix + '_velocity'], 1)
            for dimension in decoder['key_dimensions']:
                pt = next((pt for pt in dimension['values'].values() if pt in desired), None)
                velocity = next(int(v) for v, value in dimension['values'].items() if value == pt)
                add(0, dimension['note'], velocity, 1)
            add(0, 72, 90, 1)
        block = {'block': 0, 'playing': True, 'host_samples': 0, 'has_context': True, 'events': events}
        report = inspect_live.analyze({'tie_policy': 'explicit'}, [block], decoder, expected=expected)
        self.assertTrue(report['pt_sets_match'])
        self.assertEqual(report['warnings'], [])
        self.assertEqual(len(report['timing_cautions']), 16)
        self.assertEqual(len(report['commands']), 64)
        idle = {**block, 'playing': False, 'events': [events[-1]]}
        report = inspect_live.analyze({'tie_policy': 'explicit'}, [idle, block], decoder, expected=expected)
        self.assertTrue(report['pt_sets_match'])
        self.assertEqual(report['excluded_idle_musical_note_ons'], 1)
        # A second pass cannot borrow the first pass's controller/key state.
        stop = {**block, 'block': 1, 'playing': False, 'events': []}
        start = {**block, 'block': 2, 'events': [events[-1]]}
        report = inspect_live.analyze({'tie_policy': 'explicit'}, [block, stop, start], decoder,
                                     pass_number=2, expected=expected[-1:])
        self.assertFalse(report['state_complete'])
        self.assertFalse(report['pt_sets_match'])
        self.assertEqual(report['available_passes'], 2)


if __name__ == '__main__':
    unittest.main()
