#!/usr/bin/env python3
"""Read a closed MIDI Probe JSONL capture; no installation or live routing.

This tests the shared VST3 input adapter, not TCP delivery or instrument output.
Raw sequence/source indices remain evidence; equal-offset cross-list ordering
is an adapter policy, not host-observed ordering.
"""
import argparse
import json
from pathlib import Path
import inspect_midi

HERE = Path(__file__).resolve().parent


def read_capture(path):
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    if (len(rows) < 2 or rows[0].get('kind') != 'header' or rows[0].get('schema') != 1
            or rows[0].get('capture_only') is not True or rows[-1].get('kind') != 'end'):
        raise ValueError('Expected a closed MIDI Probe schema-1 capture; deactivate/remove the probe to flush its footer')
    if rows[-1]['capture_dropped']:
        raise ValueError('Capture overflow/record limit reached; this take is incomplete')
    blocks = []
    for row in rows[1:-1]:
        if row['kind'] == 'block':
            if row['block'] != len(blocks):
                raise ValueError('Missing or out-of-order capture block')
            blocks.append({**row, 'events': []})
        elif row['kind'] == 'input':
            if not blocks or row['block'] != blocks[-1]['block']:
                raise ValueError('Input without its capture block')
            blocks[-1]['events'].append(row)
        else:
            raise ValueError('Unknown trace record')
    if len(blocks) != rows[-1]['blocks']:
        raise ValueError('Truncated capture block sequence')
    for block in blocks:
        events = block['events']
        if block['input_dropped'] or block['input_invalid']:
            raise ValueError(f"Block {block['block']}: dropped, unreadable, unsupported, or invalid input; investigate raw trace")
        if len(events) != block['inputs'] or sorted(e['sequence'] for e in events) != list(range(len(events))):
            raise ValueError('Incomplete or duplicate input records')
        if any(not e['valid'] for e in events):
            raise ValueError('Invalid input in capture')
        if events != sorted(events, key=lambda e: (e['offset'], e['sequence'])):
            raise ValueError('Unexpected adapter ordering')
    return rows[0], blocks


def analyze(header, blocks, decoder, port=0, channel=1, pass_number=1, expected=None):
    starts = [i for i, b in enumerate(blocks) if b['playing'] and (i == 0 or not blocks[i - 1]['playing'])]
    if not 1 <= pass_number <= len(starts):
        raise ValueError(f'Choose --pass from 1..{len(starts)} detected playback passes')
    start = starts[pass_number - 1]
    end = next((i for i in range(start + 1, len(blocks)) if not blocks[i]['playing']), len(blocks))
    # Include preparation since the previous STOP, but never inherit a prior
    # pass's playback state. A mid-passage start must reconstruct its own state.
    begin = start
    while begin > 0 and not blocks[begin - 1]['playing']:
        begin -= 1
    notes_events, cautions, commands_seen = [], [], []
    idle_notes = 0
    kinds = {0: 9, 1: 8, 3: 11, 6: 12} # RealtimeMidiEventType -> SMF kind
    commands = inspect_midi.key_commands(decoder)
    last_host = None
    for block in blocks[begin:end]:
        if not block['has_context']:
            cautions.append(f"Block {block['block']}: missing process context")
        if block['host_samples'] < 0:
            cautions.append(f"Block {block['block']}: negative preroll sample position was saturated on the unsigned wire")
        if block['playing'] and last_host is not None and block['host_samples'] < last_host:
            cautions.append(f"Block {block['block']}: backwards sample clock during playback")
        if block['playing']:
            last_host = block['host_samples']
        groups = {}
        for event in block['events']:
            if (event['port'], event['channel']) != (port, channel):
                continue
            groups.setdefault(event['offset'], set()).add(event['source'])
            if event['type'] not in kinds:
                continue
            kind = kinds[event['type']]
            payload = (event['data1'],) if kind == 12 else (event['data1'], event['data2'])
            if not block['playing'] and kind == 9 and event['data2'] > 0 and event['data1'] not in commands:
                # Keep auditioned notes in the raw trace, but explicitly count
                # them outside this transport pass's expected musical notes.
                idle_notes += 1
                continue
            notes_events.append((event['host_samples'], port, channel, kind, payload))
            if kind == 9 and event['data2'] > 0 and event['data1'] in commands:
                commands_seen.append({k: event[k] for k in ('block', 'sequence', 'offset', 'host_samples', 'data1', 'data2', 'raw_value')})
        for offset, sources in groups.items():
            if len(sources) > 1:
                cautions.append(f"Block {block['block']} offset {offset}: cross-list tie uses parameters-before-events policy")
    notes, warnings = inspect_midi.decode_notes(decoder, notes_events)
    failures = inspect_midi.compare(notes, expected) if expected is not None else []
    complete = all(not any(n.get(k) for k in ('missing_ccs', 'invalid_ccs', 'missing_keys', 'missing_dimensions')) for n in notes)
    return {'kind': 'live-host-input-adapter-not-tcp-or-instrument-proof',
            'variant': decoder['variant'], 'pass': pass_number, 'available_passes': len(starts),
            'port': port, 'channel': channel, 'tie_policy': header['tie_policy'],
            'expectations_checked': expected is not None,
            'pt_sets_match': bool(notes) and complete and expected is not None and not failures,
            'state_complete': complete, 'timing_cautions': cautions,
            'excluded_idle_musical_note_ons': idle_notes,
            'notes': notes, 'commands': commands_seen, 'warnings': warnings, 'failures': failures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--variant', default='high-vibrato', choices=('separate', 'packed', 'keyswitch', 'velocity', 'dimensions', 'high-vibrato'))
    parser.add_argument('--port', type=int, default=0, help='Zero-based MIDI port')
    parser.add_argument('--channel', type=int, default=1, help='One-based MIDI channel')
    parser.add_argument('--pass', dest='pass_number', type=int, default=1)
    parser.add_argument('--from-note', type=int, default=1, help='Expected starting note for a mid-passage pass (1..16)')
    args = parser.parse_args()
    try:
        if not 0 <= args.port < 16 or not 1 <= args.channel <= 16:
            raise ValueError('Invalid endpoint')
        expected = json.loads((HERE / 'expected.json').read_text())
        if not 1 <= args.from_note <= len(expected):
            raise ValueError('Invalid expected starting note')
        decoder = inspect_midi.load_decoder(HERE / 'generated' / (args.variant + '.decoder.json'))
        report = analyze(*read_capture(args.capture), decoder, args.port, args.channel, args.pass_number, expected[args.from_note - 1:])
        print(json.dumps(report, indent=2))
        return 0 if report['pt_sets_match'] and not report['warnings'] and not report['timing_cautions'] else 1
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    raise SystemExit(main())
