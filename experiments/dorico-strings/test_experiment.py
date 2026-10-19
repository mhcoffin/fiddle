import copy
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

import generate
import inspect_midi


def midi(track, kind=0, division=480):
    header = b'MThd' + struct.pack('>IHHH', 6, kind, 1, division)
    return header + b'MTrk' + struct.pack('>I', len(track)) + track


class ExperimentTest(unittest.TestCase):
    def setUp(self):
        self.spec = generate.load_spec()

    def decoder(self, variant='separate'):
        return json.loads(generate.render(self.spec, variant)[1])

    def test_budget_owns_only_reserved_controllers(self):
        self.assertEqual({102, *[a['cc'] for a in self.spec['axes']]}, set(range(102, 120)))

    def test_base_product_is_exact_and_injective(self):
        states = generate.states(self.spec, 'separate')['102']['values']
        expected = {(tuple(sorted(p for p in pts if p)), length)
                    for pts in itertools.product(self.spec['shortening'], self.spec['tenuto'], self.spec['accent'])
                    for length in self.spec['lengths']}
        self.assertEqual(set(map(int, states)), set(range(90)))
        self.assertEqual({(tuple(v['techniques']), v['length']) for v in states.values()}, expected)
        self.assertEqual(states['52']['techniques'], ['pt.accent', 'pt.staccato', 'pt.tenuto'])
        self.assertEqual(states['52']['length'], 'kMedium')

    def test_packed_probe_preserves_all_eight_subsets(self):
        tables = generate.states(self.spec, 'packed')
        self.assertEqual(set(tables), {'102', '103', '106', '107', '108', '116'})
        self.assertEqual(len(generate.states(self.spec, 'separate')), 8)
        packed = tables['116']['values']
        self.assertEqual(len(packed), 8)
        self.assertEqual(packed['3']['techniques'], ['pt.flautando', 'pt.overpressure'])
        self.assertEqual(packed['7']['techniques'], ['pt.flautando', 'pt.fouette', 'pt.overpressure'])

    def test_generated_artifacts_are_current_and_deterministic(self):
        for name, data in generate.artifacts(self.spec).items():
            self.assertEqual(data, (generate.HERE / 'generated' / name).read_bytes(), name)
        self.assertEqual(generate.artifacts(self.spec), generate.artifacts(self.spec))

    def test_score_scaffold(self):
        root = ET.fromstring(generate.score_scaffold())
        self.assertEqual(root.tag, 'score-partwise')
        self.assertEqual(len(root.findall('.//measure')), 4)
        notes = root.findall('.//note')
        self.assertEqual(len(notes), 16)
        self.assertEqual([n.tag for n in notes[4].find('notations/articulations')], ['staccato', 'tenuto', 'accent'])
        self.assertEqual([n.tag for n in notes[5].find('notations/articulations')], ['staccatissimo', 'strong-accent'])

    def test_xml_switches_match_decoder_and_do_not_redefine_pts(self):
        for variant in ('separate', 'packed'):
            xml, raw_decoder = generate.render(self.spec, variant)
            root, decoder = ET.fromstring(xml), json.loads(raw_decoder)
            self.assertEqual(root.findall('.//PlayingTechniqueDefinition'), [])
            definition = root.find('.//ExpressionMapDefinition')
            self.assertEqual(definition.findtext('entityID'), decoder['map_entity_id'])
            self.assertNotEqual(definition.findtext('entityID'), 'xmap.user.Fiddle.1')
            self.assertEqual(definition.findtext('autoMutualExclusion'), 'false')
            init = {a.findtext('param1'): int(a.findtext('param2')) for a in root.findall('.//initActions/switchOnAction')}
            self.assertEqual(init, {cc: 0 for cc in decoder['tables']})
            bases = root.findall('.//playingTechniqueCombination')
            self.assertEqual(len(bases), 90)
            for switch in bases + root.findall('.//techniqueAddOn'):
                actions = switch.findall('switchOnActions/switchOnAction')
                self.assertEqual(len(actions), 1)
                cc, value = actions[0].findtext('param1'), actions[0].findtext('param2')
                raw_pts = switch.findtext('techniqueIDs')
                self.assertNotIn(',', raw_pts, 'Dorico switch conjunctions must use +, not MEG-list commas')
                pts = set(raw_pts.split('+'))
                self.assertEqual(pts - {'pt.natural'}, set(decoder['tables'][cc]['values'][value]['techniques']))
                for action in switch.findall('switchOffActions/switchOffAction'):
                    self.assertEqual(action.findtext('param1'), cc)
                    self.assertEqual(action.findtext('param2'), '0')
                if switch.tag == 'playingTechniqueCombination':
                    self.assertEqual(switch.findall('switchOffActions/switchOffAction'), [])
            for action in root.findall('.//switchOnAction') + root.findall('.//switchOffAction'):
                self.assertEqual(action.findtext('type'), 'kControlChange')
                self.assertIn(int(action.findtext('param1')), range(102, 120))
                self.assertIn(int(action.findtext('param2')), range(128))

    def test_megs_do_not_exclude_independent_modifiers(self):
        root = ET.fromstring(generate.render(self.spec, 'packed')[0])
        groups = [set(p.strip() for p in m.findtext('techniqueIDs').split(',')) for m in root.findall('.//mutualExclusionGroup')]
        self.assertIn({'pt.staccato', 'pt.staccatissimo'}, groups)
        self.assertIn({'pt.accent', 'pt.marcato'}, groups)
        self.assertTrue(all(not {'pt.tenuto', 'pt.flautando', 'pt.overpressure', 'pt.fouette'} & g for g in groups))
        self.assertEqual([g for g in groups if 'pt.natural' in g], [{'pt.natural', 'pt.sulPonticello', 'pt.sulTasto', 'pt.pdlt', 'pt.bowOnBridge', 'pt.bowOnTailpiece'}])

    def test_revision_and_host_compound_spelling(self):
        for variant in generate.VARIANTS:
            root = ET.fromstring(generate.render(self.spec, variant)[0])
            definition = root.find('.//ExpressionMapDefinition')
            revision = {'keyswitch': '3', 'velocity': '4', 'dimensions': '5', 'high-vibrato': '6'}.get(variant, '2')
            self.assertTrue(definition.findtext('entityID').endswith('.r' + revision))
            self.assertTrue(definition.findtext('name').endswith(' r' + revision))
            bases = [e.findtext('techniqueIDs') for e in root.findall('.//playingTechniqueCombination')]
            self.assertIn('pt.accent+pt.staccato+pt.tenuto', bases)
            if variant == 'packed':
                self.assertIn('pt.flautando+pt.fouette+pt.overpressure',
                              [e.findtext('techniqueIDs') for e in root.findall('.//techniqueAddOn')])
            for meg in root.findall('.//mutualExclusionGroup'):
                self.assertNotIn('+', meg.findtext('techniqueIDs'))
                self.assertIn(', ', meg.findtext('techniqueIDs'))

    def test_map_decoder_digest_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            for name, data in generate.artifacts(self.spec).items():
                (path / name).write_bytes(data)
            self.assertEqual(inspect_midi.load_decoder(path / 'separate.decoder.json')['variant'], 'separate')
            (path / 'separate.doricolib').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'digest mismatch'):
                inspect_midi.load_decoder(path / 'separate.decoder.json')

    def test_bad_spec_rejected(self):
        mutations = [lambda s: s['axes'][0].update(cc=102),
                     lambda s: s['axes'][0].update(cc=101),
                     lambda s: s['axes'][0].update(values=[None, 'pt.tenuto']),
                     lambda s: s['packed_probe'].update(axis_ids=['flautando', 'flautando']),
                     lambda s: s.update(lengths=['bad'])]
        for mutate in mutations:
            spec = copy.deepcopy(self.spec)
            mutate(spec)
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'spec.json'
                path.write_text(json.dumps(spec))
                with self.assertRaises(ValueError):
                    generate.load_spec(path)

    def test_midi_running_status_ports_and_note_on_zero(self):
        track = bytes([0, 0xFF, 0x21, 1, 3, 0, 0xB2, 106, 1, 0, 108, 2,
                       0, 0x92, 60, 90, 120, 60, 0, 0, 0xFF, 0x2F, 0])
        ppqn, tracks = inspect_midi.read_midi(midi(track))
        self.assertEqual(ppqn, 480)
        self.assertEqual(tracks[0][0], (0, 3, 3, 0xB, (106, 1)))
        self.assertEqual(tracks[0][1], (0, 3, 3, 0xB, (108, 2)))
        notes, warnings = inspect_midi.decode_notes(self.decoder(), tracks[0])
        self.assertEqual(len(notes), 1)
        self.assertEqual(notes[0]['techniques'], ['pt.nonVibrato', 'pt.sulTasto'])
        self.assertIn(102, notes[0]['missing_ccs'])
        self.assertEqual(warnings, [])

    def test_midi_meta_sysex_and_multibyte_delta(self):
        track = bytes([0, 0xFF, 0x51, 3, 7, 0xA1, 0x20, 0, 0xF0, 2, 1, 0xF7,
                       0x81, 0, 0x90, 60, 90, 0, 0xFF, 0x2F, 0])
        self.assertEqual(inspect_midi.read_midi(midi(track))[1][0][0][0], 128)

    def test_malformed_midi_rejected(self):
        bad = [b'', midi(b'\0\x90\x3c'), midi(b'\0\x3c\x40\0\xff\x2f\0'),
               midi(b'\0\x90\xff\x40\0\xff\x2f\0'), midi(b'\0\xff\x2f\0', kind=2),
               midi(b'\0\xff\x2f\0', division=0x8001), midi(b'\0\x90\x3c\x40'),
               midi(b'\xff\xff\xff\xff\x7f')]
        for data in bad:
            with self.assertRaises(ValueError):
                inspect_midi.read_midi(data)

    def test_channel_port_isolation_invalid_values_and_no_late_repair(self):
        events = [(0, 0, 1, 0xB, (106, 1)), (0, 0, 1, 0x9, (60, 90)),
                  (0, 1, 1, 0x9, (60, 90)), (0, 0, 2, 0x9, (60, 90)),
                  (0, 0, 1, 0xB, (106, 127)), (0, 0, 1, 0x9, (60, 90)),
                  (0, 0, 1, 0xB, (106, 3))]
        notes, warnings = inspect_midi.decode_notes(self.decoder(), events)
        self.assertEqual(notes[0]['techniques'], ['pt.nonVibrato'])
        self.assertEqual(notes[1]['techniques'], [])
        self.assertEqual(notes[2]['techniques'], [])
        self.assertEqual(notes[3]['invalid_ccs'], [106])
        self.assertEqual(len([w for w in warnings if 'unknown CC' in w]), 1)
        self.assertEqual(len([w for w in warnings if 'follows note' in w]), 2)

    def test_first_note_length_update_after_note_on_is_flagged_not_reordered(self):
        decoder = self.decoder()
        events = [(0, 0, 1, 0xB, (int(cc), 0)) for cc in decoder['tables']]
        events += [(0, 0, 1, 0x9, (72, 100)), (0, 0, 1, 0xB, (102, 2))]
        notes, warnings = inspect_midi.decode_notes(decoder, events)
        self.assertEqual(notes[0]['length'], 'kVeryShort')
        self.assertEqual(len(warnings), 1)
        self.assertIn('CC102=2 follows note 1', warnings[0])

    def test_synthetic_transition_contract_both_variants(self):
        # Synthetic serialization test only: does NOT model Dorico selection.
        expected = json.loads((generate.HERE / 'expected.json').read_text())
        for variant in generate.VARIANTS:
            decoder = self.decoder(variant)
            events = []
            for i, row in enumerate(expected):
                desired = set(row['techniques'])
                for cc, table in decoder['tables'].items():
                    relevant = {pt for v in table['values'].values() for pt in v['techniques']}
                    candidates = [int(value) for value, state in table['values'].items()
                                  if set(state['techniques']) == desired & relevant and state.get('length', 'kMedium') == 'kMedium']
                    self.assertEqual(len(candidates), 1)
                    events.append((i * 480, 0, 1, 0xB, (int(cc), candidates[0])))
                for key in decoder.get('key_switches', []):
                    prefix = 'on' if key['technique'] in desired else 'off'
                    pitch, velocity = key[prefix + '_note'], key.get(prefix + '_velocity', 127)
                    events.append((i * 480, 0, 1, 0x9, (pitch, velocity)))
                    events.append((i * 480, 0, 1, 0x8, (pitch, 0)))
                for dimension in decoder.get('key_dimensions', []):
                    selected = desired.intersection(p for p in dimension['values'].values() if p)
                    self.assertLessEqual(len(selected), 1)
                    pt = next(iter(selected), None)
                    velocity = next(int(v) for v, value in dimension['values'].items() if value == pt)
                    events.append((i * 480, 0, 1, 0x9, (dimension['note'], velocity)))
                events.append((i * 480, 0, 1, 0x9, (60, 90)))
            notes, warnings = inspect_midi.decode_notes(decoder, events)
            self.assertEqual(warnings, [])
            self.assertEqual(inspect_midi.compare(notes, expected), [])
            notes[11]['techniques'].remove('pt.overpressure')
            self.assertTrue(inspect_midi.compare(notes, expected))

    def test_comparison_requires_all_notes_and_complete_state(self):
        expected = [{'label': 'empty', 'techniques': []}]
        self.assertTrue(inspect_midi.compare([], expected))
        notes, _ = inspect_midi.decode_notes(self.decoder(), [(0, 0, 1, 0x9, (60, 90))])
        self.assertTrue(inspect_midi.compare(notes, expected))

    def test_cli_end_to_end_and_wrong_variant(self):
        # A complete synthetic MIDI file proves CLI wiring, not Dorico behavior.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            track = bytearray()
            for cc in self.decoder('packed')['tables']:
                track.extend([0, 0xB0, int(cc), 0])
            track.extend([0, 0x90, 72, 90, 120, 0x80, 72, 0, 0, 0xFF, 0x2F, 0])
            capture = path / 'capture.mid'
            capture.write_bytes(midi(track))
            expected = path / 'expected.json'
            expected.write_text(json.dumps([{'label': 'unmarked', 'techniques': [], 'pitch': 72}]))
            command = [sys.executable, str(generate.HERE / 'inspect_midi.py'), str(capture), '--variant', 'packed', '--expect', str(expected)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(json.loads(result.stdout)['passed'])
            command[command.index('packed')] = 'separate'
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(json.loads(result.stdout)['notes'][0]['missing_ccs'], [117, 118])

    def test_observed_r2_exports_are_not_mistaken_for_full_acceptance(self):
        report = json.loads((generate.HERE / 'results' / 'round2.json').read_text())
        expected = json.loads((generate.HERE / 'expected.json').read_text())
        for variant, capture in report['captures'].items():
            self.assertEqual(self.decoder(variant)['map_sha256'], capture['map_sha256'])
            self.assertEqual(self.decoder(variant)['spec_fingerprint'], capture['spec_fingerprint'])
            notes, warnings = inspect_midi.decode_notes(self.decoder(variant), capture['events'])
            self.assertEqual(notes, capture['notes'])
            self.assertEqual(warnings, capture['warnings'])
            failures = inspect_midi.compare(notes, expected)
            self.assertEqual(failures, capture['failures'])
            self.assertEqual(len(failures), 0 if variant == 'separate' else 3)
            self.assertEqual(len(warnings), 1)
            self.assertIn('CC102=2 follows note 1', warnings[0])
            self.assertEqual(notes[4]['techniques'], ['pt.accent', 'pt.staccato', 'pt.tenuto'])
            self.assertFalse(capture['full_acceptance_pass'])

    def test_keyswitch_xml_and_decoder_contract(self):
        xml, raw = generate.render(self.spec, 'keyswitch')
        decoder, root = json.loads(raw), ET.fromstring(xml)
        self.assertEqual(decoder['schema_version'], 2)
        self.assertEqual(set(decoder['tables']), {'102', '103', '106', '107', '108'})
        self.assertEqual(len(root.findall('.//playingTechniqueCombination')), 90)
        self.assertEqual(len(root.findall('.//techniqueAddOn')), 16)
        init = root.findall('.//initActions/switchOnAction')
        self.assertEqual([int(a.findtext('param1')) for a in init if a.findtext('type') == 'kKeySwitch'], [1, 3, 5])
        addons = {a.findtext('techniqueIDs'): a for a in root.findall('.//techniqueAddOn')}
        for key in decoder['key_switches']:
            addon = addons[key['technique']]
            for kind, field in [('On', 'on_note'), ('Off', 'off_note')]:
                action = addon.find(f'switch{kind}Actions/switch{kind}Action')
                self.assertEqual(action.findtext('type'), 'kKeySwitch')
                self.assertEqual(int(action.findtext('param1')), key[field])
                self.assertEqual(action.findtext('param2'), '127')
        self.assertEqual(inspect_midi.load_decoder(generate.HERE / 'generated/keyswitch.decoder.json'), decoder)

    def check_key_subsets_and_pulse_releases(self, variant):
        decoder = self.decoder(variant)
        for mask in range(8):
            events = [(0, 0, 1, 0xB, (int(cc), 0)) for cc in decoder['tables']]
            wanted = []
            for bit, key in enumerate(decoder['key_switches']):
                active = bool(mask & (1 << bit))
                if active:
                    wanted.append(key['technique'])
                prefix = 'on' if active else 'off'
                pitch, velocity = key[prefix + '_note'], key.get(prefix + '_velocity', 127)
                events += [(0, 0, 1, 0x9, (pitch, velocity)), (0, 0, 1, 0x9, (pitch, velocity)),
                           (1, 0, 1, 0x8, (pitch, 0)), (1, 0, 1, 0x9, (pitch, 0))]
            events.append((2, 0, 1, 0x9, (72, 90)))
            notes, warnings = inspect_midi.decode_notes(decoder, events)
            self.assertEqual(warnings, [])
            self.assertEqual(len(notes), 1)
            self.assertEqual(notes[0]['techniques'], sorted(wanted))
            self.assertEqual(notes[0]['missing_keys'], [])

    def test_keyswitch_subsets_and_pulse_releases(self):
        self.check_key_subsets_and_pulse_releases('keyswitch')

    def test_velocity_subsets_and_pulse_releases(self):
        self.check_key_subsets_and_pulse_releases('velocity')

    def test_dimensions_subsets_and_pulse_releases(self):
        self.check_key_subsets_and_pulse_releases('dimensions')

    def test_keyswitch_unknown_state_isolation_and_late_command(self):
        decoder = self.decoder('keyswitch')
        events = [(0, 0, 1, 0x9, (0, 127)), (1, 0, 1, 0x9, (72, 90)),
                  (1, 1, 1, 0x9, (72, 90)), (1, 0, 2, 0x9, (72, 90)),
                  (1, 0, 1, 0x9, (1, 127)), (2, 0, 1, 0x9, (72, 90)),
                  (3, 0, 1, 0x9, (6, 90))]
        notes, warnings = inspect_midi.decode_notes(decoder, events)
        self.assertEqual(notes[0]['techniques'], ['pt.flautando'])
        self.assertEqual(len(notes[0]['missing_keys']), 2)
        for note in notes[1:3]:
            self.assertEqual(note['techniques'], [])
            self.assertEqual(len(note['missing_keys']), 3)
        self.assertEqual(notes[3]['techniques'], [])
        self.assertIn('keyswitch 1 follows note 1', warnings[0])
        self.assertIn('outside', warnings[1])
        # Even with all CCs known, unknown keys must fail comparison.
        notes[0].update(missing_ccs=[], invalid_ccs=[])
        self.assertTrue(inspect_midi.compare(notes[:1], [{'label': 'test', 'techniques': ['pt.flautando']}]))

    def test_keyswitch_spec_rejects_collisions_and_invalid_controls(self):
        original = generate.load_key_spec(self.spec)
        mutations = [lambda p: p['modifiers'][0].update(on_note=72),
                     lambda p: p['modifiers'][0].update(on_note=1),
                     lambda p: p['modifiers'][0].update(axis_id='vibrato'),
                     lambda p: p.update(velocity=0),
                     lambda p: p.update(musical_range=[103, 55])]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'keys.json'
            for mutate in mutations:
                probe = copy.deepcopy(original)
                mutate(probe)
                path.write_text(json.dumps(probe))
                with self.assertRaises(ValueError):
                    generate.load_key_spec(self.spec, path)

    def check_key_cli_and_cross_track_ambiguity(self, variant):
        decoder = self.decoder(variant)
        commands = bytearray()
        for cc in decoder['tables']:
            commands.extend([0, 0xB0, int(cc), 0])
        for key in decoder['key_switches']:
            commands.extend([0, 0x90, key['off_note'], key.get('off_velocity', 127), 0, 0x80, key['off_note'], 0])
        for dimension in decoder.get('key_dimensions', []):
            velocity = next(int(v) for v, pt in dimension['values'].items() if pt is None)
            commands.extend([0, 0x90, dimension['note'], velocity, 0, 0x80, dimension['note'], 0])
        music = bytes([0, 0x90, 72, 90, 120, 0x80, 72, 0])
        end = b'\0\xff\x2f\0'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.mid'
            command = [sys.executable, str(generate.HERE / 'inspect_midi.py'), str(path), '--variant', variant]
            path.write_bytes(midi(commands + music + end))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(json.loads(result.stdout)['notes']), 1)
            path.write_bytes(b'MThd' + struct.pack('>IHHH', 6, 1, 2, 480)
                             + b'MTrk' + struct.pack('>I', len(commands + end)) + commands + end
                             + b'MTrk' + struct.pack('>I', len(music + end)) + music + end)
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('span multiple tracks', result.stderr)

    def test_keyswitch_cli_and_cross_track_ambiguity(self):
        self.check_key_cli_and_cross_track_ambiguity('keyswitch')

    def test_velocity_cli_and_cross_track_ambiguity(self):
        self.check_key_cli_and_cross_track_ambiguity('velocity')

    def test_dimensions_cli_and_cross_track_ambiguity(self):
        self.check_key_cli_and_cross_track_ambiguity('dimensions')

    def test_high_vibrato_cli_and_cross_track_ambiguity(self):
        self.check_key_cli_and_cross_track_ambiguity('high-vibrato')

    def test_high_vibrato_map_contract(self):
        decoder = self.decoder('high-vibrato')
        self.assertEqual(decoder['schema_version'], 4)
        self.assertEqual(set(decoder['tables']), {'102', '103', '107', '108'})
        self.assertEqual(decoder['key_switches'], self.decoder('dimensions')['key_switches'])
        dim, = decoder['key_dimensions']
        self.assertEqual(dim, {'axis_id': 'vibrato', 'note': 127, 'values': {
            '1': None, '2': 'pt.nonVibrato', '64': 'pt.conVibrato', '126': 'pt.vibrato', '127': 'pt.wideVibrato'}})
        root = ET.fromstring(generate.render(self.spec, 'high-vibrato')[0])
        init = root.findall('.//initActions/switchOnAction')
        self.assertIn(('127', '1'), [(a.findtext('param1'), a.findtext('param2')) for a in init])
        self.assertFalse(any(a.findtext('type') == 'kControlChange' and a.findtext('param1') == '106' for a in root.findall('.//switchOnAction')))
        for velocity, pt in dim['values'].items():
            if pt is None:
                continue
            addon, = [a for a in root.findall('.//techniqueAddOn') if a.findtext('techniqueIDs') == pt]
            self.assertEqual(addon.findtext('switchOnActions/switchOnAction/type'), 'kKeySwitch')
            self.assertEqual(addon.findtext('switchOnActions/switchOnAction/param1'), '127')
            self.assertEqual(addon.findtext('switchOnActions/switchOnAction/param2'), velocity)
            self.assertEqual(addon.findtext('switchOffActions/switchOffAction/param2'), '1')
        groups = [set(m.findtext('techniqueIDs').split(', ')) for m in root.findall('.//mutualExclusionGroup')]
        self.assertIn(set(dim['values'].values()) - {None}, groups)
        self.assertEqual(inspect_midi.load_decoder(generate.HERE / 'generated/high-vibrato.decoder.json'), decoder)

    def test_scalar_replaces_group_and_never_infers_restoration(self):
        decoder = self.decoder('high-vibrato')
        velocities = [1, 2, 64, 126, 127, 1, 2]
        events = []
        for i, velocity in enumerate(velocities):
            events += [(i * 10, 0, 1, 9, (127, velocity)),
                       (i * 10, 0, 1, 8, (127, 126)),
                       (i * 10, 0, 1, 9, (127, 0)),
                       (i * 10 + 1, 0, 1, 9, (72, 90))]
        notes, warnings = inspect_midi.decode_notes(decoder, events)
        self.assertEqual(warnings, [])
        wanted = [[], ['pt.nonVibrato'], ['pt.conVibrato'], ['pt.vibrato'], ['pt.wideVibrato'], [], ['pt.nonVibrato']]
        self.assertEqual([n['techniques'] for n in notes], wanted)
        self.assertTrue(all(n['missing_dimensions'] == [] for n in notes))

    def test_scalar_missing_unknown_late_and_endpoint_isolation(self):
        decoder = self.decoder('high-vibrato')
        events = [(0, 0, 1, 9, (72, 90)), (0, 0, 1, 9, (127, 1)),
                  (1, 0, 1, 9, (127, 2)), (2, 0, 1, 9, (127, 65)),
                  (3, 0, 1, 9, (72, 90)), (3, 0, 2, 9, (72, 90)),
                  (3, 1, 1, 9, (72, 90))]
        notes, warnings = inspect_midi.decode_notes(decoder, events)
        self.assertEqual(notes[0]['missing_dimensions'], ['vibrato'])
        self.assertEqual(notes[1]['techniques'], ['pt.nonVibrato'])
        for n in notes[2:]:
            self.assertEqual(n['techniques'], [])
            self.assertEqual(n['missing_dimensions'], ['vibrato'])
        self.assertEqual(len(warnings), 2)
        self.assertIn('follows note 1', warnings[0])
        self.assertIn('127/65', warnings[1])
        notes[0].update(missing_ccs=[], invalid_ccs=[], missing_keys=[])
        self.assertTrue(inspect_midi.compare(notes[:1], [{'label': 'missing', 'techniques': []}]))

    def test_scalar_spec_and_manifest_validation(self):
        original = generate.load_key_spec(self.spec, generate.HERE / 'high-vibrato-probe.json')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            for mutation in [lambda d: d.update(note=0), lambda d: d.update(note=72),
                             lambda d: d.update(velocities=[1, 2, 3, 4, 0]),
                             lambda d: d.update(velocities=[1, 2, 3, 4, 4]),
                             lambda d: d.update(velocities=[1, 2]), lambda d: d.update(axis_id='flautando')]:
                spec = copy.deepcopy(original)
                mutation(spec['dimensions'][0])
                (path / 'spec.json').write_text(json.dumps(spec))
                with self.assertRaises(ValueError):
                    generate.load_key_spec(self.spec, path / 'spec.json')
            xml, raw = generate.render(self.spec, 'high-vibrato')
            (path / 'high-vibrato.doricolib').write_bytes(xml)
            for mutation in [lambda d: d.update(note=0), lambda d: d.update(note=72),
                             lambda d: d['values'].update({'0': 'pt.other'}),
                             lambda d: d['values'].update({'1': 'pt.nonVibrato'}),
                             lambda d: d['values'].update({'2': 'pt.flautando'})]:
                decoder = json.loads(raw)
                mutation(decoder['key_dimensions'][0])
                (path / 'high-vibrato.decoder.json').write_text(json.dumps(decoder))
                with self.assertRaises(ValueError):
                    inspect_midi.load_decoder(path / 'high-vibrato.decoder.json')

    def test_observed_e_r5_reproduces_all_pt_sets(self):
        _, tracks = inspect_midi.read_midi((generate.HERE / 'dimensions-r5.mid').read_bytes())
        notes, warnings = inspect_midi.decode_notes(self.decoder('dimensions'), tracks[1])
        self.assertEqual(inspect_midi.compare(notes, json.loads((generate.HERE / 'expected.json').read_text())), [])
        self.assertEqual(len(warnings), 1)
        self.assertIn('CC102=2 follows note 1', warnings[0])

    def test_velocity_xml_contract(self):
        xml, raw = generate.render(self.spec, 'velocity')
        decoder, root = json.loads(raw), ET.fromstring(xml)
        self.assertEqual(decoder['schema_version'], 3)
        self.assertEqual(set(decoder['tables']), {'102', '103', '106', '107', '108'})
        self.assertEqual(root.findtext('.//allowMultipleNotesAtSamePitch'), 'true')
        self.assertEqual(len(root.findall('.//playingTechniqueCombination')), 90)
        self.assertEqual(len(root.findall('.//techniqueAddOn')), 16)
        init = root.findall('.//initActions/switchOnAction')
        self.assertEqual([(int(a.findtext('param1')), int(a.findtext('param2'))) for a in init if a.findtext('type') == 'kKeySwitch'], [(0, 2), (0, 64), (0, 127)])
        addons = {a.findtext('techniqueIDs'): a for a in root.findall('.//techniqueAddOn')}
        for key in decoder['key_switches']:
            addon = addons[key['technique']]
            for kind, prefix in [('On', 'on'), ('Off', 'off')]:
                action = addon.find(f'switch{kind}Actions/switch{kind}Action')
                self.assertEqual(action.findtext('type'), 'kKeySwitch')
                self.assertEqual(int(action.findtext('param1')), key[prefix + '_note'])
                self.assertEqual(int(action.findtext('param2')), key[prefix + '_velocity'])
        self.assertEqual(inspect_midi.load_decoder(generate.HERE / 'generated/velocity.decoder.json'), decoder)

    def test_velocity_unknown_zero_late_and_endpoint_isolation(self):
        decoder = self.decoder('velocity')
        events = [(0, 0, 1, 0x9, (0, 1)), (0, 0, 1, 0x9, (0, 63)),
                  (0, 0, 1, 0x9, (0, 126)), (0, 0, 1, 0x9, (0, 0)),
                  (0, 0, 1, 0x8, (0, 64)), (1, 0, 1, 0x9, (72, 90)),
                  (1, 1, 1, 0x9, (72, 90)), (1, 0, 2, 0x9, (72, 90)),
                  (1, 0, 1, 0x9, (0, 2)), (2, 0, 1, 0x9, (0, 65)),
                  (3, 0, 1, 0x9, (72, 90))]
        notes, warnings = inspect_midi.decode_notes(decoder, events)
        self.assertEqual(len(notes), 4)
        self.assertEqual(notes[0]['techniques'], ['pt.flautando', 'pt.fouette', 'pt.overpressure'])
        self.assertEqual(notes[0]['missing_keys'], [])
        for note in notes[1:3]:
            self.assertEqual(note['techniques'], [])
            self.assertEqual(len(note['missing_keys']), 3)
        self.assertEqual(notes[3]['techniques'], ['pt.fouette', 'pt.overpressure'])
        self.assertEqual(len(warnings), 2)
        self.assertIn('follows note 1', warnings[0])
        self.assertIn('unknown keyswitch pitch/velocity 0/65', warnings[1])

    def test_velocity_spec_and_decoder_reject_invalid_codes(self):
        original = generate.load_key_spec(self.spec, generate.HERE / 'velocity-probe.json')
        mutations = [lambda p: p['modifiers'][0].update(on_velocity=0),
                     lambda p: p['modifiers'][0].update(on_velocity=128),
                     lambda p: p['modifiers'][0].update(on_velocity=63),
                     lambda p: p['modifiers'][0].update(on_note=72)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            xml, raw = generate.render(self.spec, 'velocity')
            (path / 'velocity.doricolib').write_bytes(xml)
            for mutate in mutations:
                probe = copy.deepcopy(original)
                mutate(probe)
                (path / 'probe.json').write_text(json.dumps(probe))
                with self.assertRaises(ValueError):
                    generate.load_key_spec(self.spec, path / 'probe.json')
                decoder = json.loads(raw)
                # Apply the same invalid specification fields to its manifest.
                for key, modifier in zip(decoder['key_switches'], probe['modifiers']):
                    key.update(modifier)
                (path / 'velocity.decoder.json').write_text(json.dumps(decoder))
                with self.assertRaises(ValueError):
                    inspect_midi.load_decoder(path / 'velocity.decoder.json')

    def test_observed_c_r3_remains_reproducible(self):
        decoder = self.decoder('keyswitch')
        self.assertEqual(decoder['map_sha256'], 'e2473d0cc5ad63bccb3f596aaf18f38eb22f087522d1b73fe2635c5a9c742d7a')
        _, tracks = inspect_midi.read_midi((generate.HERE / 'keyswitch-r3.mid').read_bytes())
        notes, warnings = inspect_midi.decode_notes(decoder, tracks[1])
        self.assertEqual(inspect_midi.compare(notes, json.loads((generate.HERE / 'expected.json').read_text())), [])
        self.assertEqual(len(warnings), 1)
        self.assertIn('CC102=2 follows note 1', warnings[0])

    def test_observed_d_r4_missing_commands_remain_a_failure(self):
        decoder = self.decoder('velocity')
        self.assertEqual(decoder['map_sha256'], '9fc5cd60e54a7c865a4670e8498ad74817b245e3bddfeecfbd0dd3b0abe6275a')
        _, tracks = inspect_midi.read_midi((generate.HERE / 'velocity-r4.mid').read_bytes())
        notes, warnings = inspect_midi.decode_notes(decoder, tracks[1])
        expected = json.loads((generate.HERE / 'expected.json').read_text())
        failures = inspect_midi.compare(notes, expected)
        self.assertEqual(len(failures), 3)
        self.assertEqual([n['index'] for n, e in zip(notes, expected) if set(n['techniques']) != set(e['techniques'])], [11, 12, 13])
        self.assertEqual(len(warnings), 1)
        self.assertIn('CC102=2 follows note 1', warnings[0])
        self.assertTrue(all(not n['missing_keys'] for n in notes))
        commands = [e[4] for e in tracks[1] if e[3] == 9 and e[4][0] == 0 and e[4][1] > 0]
        self.assertEqual(commands[:3], [(0, 2), (0, 64), (0, 127)])
        self.assertNotIn((0, 126), commands)

    def test_dimensions_change_only_command_pitches_and_identity(self):
        previous = self.decoder('velocity')
        decoder = self.decoder('dimensions')
        self.assertEqual(decoder['schema_version'], 3)
        self.assertEqual(decoder['tables'], previous['tables'])
        self.assertEqual(decoder['musical_range'], previous['musical_range'])
        for pitch, (old, new) in enumerate(zip(previous['key_switches'], decoder['key_switches'])):
            self.assertEqual(new, {**old, 'on_note': pitch, 'off_note': pitch})
        root = ET.fromstring(generate.render(self.spec, 'dimensions')[0])
        old_root = ET.fromstring(generate.render(self.spec, 'velocity')[0])
        self.assertEqual(root.findtext('.//allowMultipleNotesAtSamePitch'), 'true')
        self.assertEqual(ET.tostring(root.find('.//playingTechniqueCombinations')),
                         ET.tostring(old_root.find('.//playingTechniqueCombinations')))
        self.assertEqual(len(root.findall('.//techniqueAddOn')), 16)
        init = [(int(a.findtext('param1')), int(a.findtext('param2')))
                for a in root.findall('.//initActions/switchOnAction') if a.findtext('type') == 'kKeySwitch']
        self.assertEqual(init, [(0, 2), (1, 64), (2, 127)])
        addons = {a.findtext('techniqueIDs'): a for a in root.findall('.//techniqueAddOn')}
        for key in decoder['key_switches']:
            for kind, prefix in [('On', 'on'), ('Off', 'off')]:
                action = addons[key['technique']].find(f'switch{kind}Actions/switch{kind}Action')
                self.assertEqual(action.findtext('type'), 'kKeySwitch')
                self.assertEqual(int(action.findtext('param1')), key[prefix + '_note'])
                self.assertEqual(int(action.findtext('param2')), key[prefix + '_velocity'])
        self.assertEqual(inspect_midi.load_decoder(generate.HERE / 'generated/dimensions.decoder.json'), decoder)

    def test_dimensions_wrong_velocity_is_not_a_different_dimensions_command(self):
        decoder = self.decoder('dimensions')
        events = [(0, 0, 1, 9, (k['off_note'], k['off_velocity'])) for k in decoder['key_switches']]
        # 63 is valid on pitch 1, not pitch 0. Do not match on velocity alone.
        events += [(1, 0, 1, 9, (0, 63)), (2, 0, 1, 9, (72, 90)),
                   (3, 0, 1, 9, (1, 63)), (3, 0, 1, 8, (1, 64)),
                   (3, 0, 1, 9, (1, 0)), (4, 0, 1, 9, (72, 90)),
                   (4, 0, 2, 9, (72, 90)), (4, 1, 1, 9, (72, 90)),
                   (4, 0, 1, 9, (1, 64)), (5, 0, 1, 9, (72, 90))]
        notes, warnings = inspect_midi.decode_notes(decoder, events)
        self.assertEqual(notes[0]['techniques'], [])
        self.assertEqual(notes[0]['missing_keys'], [])
        self.assertEqual(notes[1]['techniques'], ['pt.overpressure'])
        for note in notes[2:4]:
            self.assertEqual(note['techniques'], [])
            self.assertEqual(len(note['missing_keys']), 3)
        self.assertEqual(notes[4]['techniques'], [])
        self.assertEqual(len(warnings), 2)
        self.assertIn('unknown keyswitch pitch/velocity 0/63', warnings[0])
        self.assertIn('follows note 2', warnings[1])


if __name__ == '__main__':
    unittest.main()
