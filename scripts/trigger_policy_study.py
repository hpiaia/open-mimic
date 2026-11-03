#!/usr/bin/env python3
"""Compare trigger event policies on the owner's verified recordings.

For every pad with at least two complete sessions, the response profile is fitted
from one session and the *other* session is replayed through the actual C++ engine.
Reported per take: isolated strong hits found by an independent quiet-gap detector,
events each policy emitted, and how the stage-two rule labelled them. Guided takes
carry no strike timestamps, so counts are mechanism evidence, not recall claims.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import statistics
import subprocess
import tempfile

from calibrate_pad import fit_session, global_channel, native_profile, verified_session
from inspect_pad_sequence import read_records

LABEL = {'head-varied': 1, 'rim-varied': 2, 'rimshot-varied': 3}
STRONG_PEAK = 40.0


def legacy_profile(profile):
    """Version 1 text for the pre-study replay tool (no zone rule)."""
    lines = native_profile(profile).splitlines()
    return '\n'.join(['OPEN_MIMIC_RESPONSE 1'] + [line for line in lines[1:] if not line.startswith('rule ')]) + '\n'


def frames_file(path, metadata, destination):
    head = global_channel(metadata['head'])
    rim = global_channel(metadata.get('input_rim', metadata.get('rim')))
    zones = metadata['zone_count']
    position = 0
    rows = []
    with destination.open('w') as stream:
        for _, batches, flags, _ in read_records(path.parent / metadata['capture']):
            if flags:
                stream.write('-1\n')
                rows.append(None)
                position += 1
                continue
            for raw in batches[head // 16]:
                channels = {word >> 12: word & 4095 for word in raw}
                pair = (channels[head % 16], channels[rim % 16] if zones == 2 else 0)
                stream.write(f'{position} {pair[0]} {pair[1]}\n')
                rows.append(pair)
                position += 1
    return head, rim, zones, rows


def isolated_hits(rows, sensors, zones):
    """Strong onsets preceded by 32 quiet frames: the hits any policy must find."""
    baseline = [sensors[c]['baseline'] for c in range(2)]
    threshold = [sensors[c]['threshold'] for c in range(2)]
    above = [None if r is None else any(abs(r[c] - baseline[c]) >= threshold[c] for c in range(zones)) for r in rows]
    hits = []
    quiet = 0
    index = 0
    while index < len(rows):
        if above[index] is None:
            quiet = 0
            index += 1
            continue
        if not above[index]:
            quiet += 1
            index += 1
            continue
        if quiet >= 32:
            peak = max(abs(rows[j][c] - baseline[c]) for j in range(index, min(len(rows), index + 32))
                       if rows[j] is not None for c in range(zones))
            if peak >= STRONG_PEAK:
                hits.append(index)
        quiet = 0
        index += 1
    return hits


def replay(tool, model, head, rim, zones, profile_path, frames, settings_path):
    arguments = [str(tool), model or '-', str(head), str(rim), str(zones), str(profile_path), str(frames)]
    if settings_path is not None:
        arguments.append(str(settings_path))
    result = subprocess.run(arguments, text=True, capture_output=True, timeout=300)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip())
    events = []
    summary = None
    for line in result.stdout.splitlines():
        row = json.loads(line)
        if row['kind'] == 'event':
            events.append(row)
        elif row['kind'] == 'summary':
            summary = row
    return events, summary


def study_take(take, directory, profile, tools, settings_text, temporary):
    path, metadata = take
    frames = temporary / (metadata['phase'] + '.frames.txt')
    head, rim, zones, rows = frames_file(path, metadata, frames)
    reference = isolated_hits(rows, profile['sensors'], zones)
    result = {'phase': metadata['phase'], 'frames': sum(r is not None for r in rows), 'isolated_strong_hits': len(reference)}
    label = LABEL.get(metadata['phase'])
    for name, (tool, text, settings) in tools.items():
        profile_path = temporary / f'{name}.profile'
        profile_path.write_text(text)
        settings_path = None
        if settings is not None:
            settings_path = temporary / f'{name}.trigger'
            settings_path.write_text(settings)
        events, summary = replay(tool, None, head, rim, zones, profile_path, frames, settings_path)
        strong = [e for e in events if max(e.get('head_peak', e.get('velocity', 0)), e.get('rim_peak', 0)) >= STRONG_PEAK] \
            if 'head_peak' in (events[0] if events else {}) else events
        zones_emitted = Counter(e['zone'] for e in events)
        entry = {'events': len(events), 'zones': {str(z): n for z, n in sorted(zones_emitted.items())},
                 'rejected': summary['rejected'], 'clipped': summary['clipped']}
        if 'head_peak' in (events[0] if events else {}):
            entry['strong_events'] = len(strong)
            entry['rule_zones_strong'] = {str(z): n for z, n in sorted(Counter(e['rule_zone'] for e in strong).items())}
            if label and strong:
                entry['label_agreement_strong'] = round(sum(e['zone'] == label for e in strong) / len(strong), 3)
            # Events less than 10 ms after another event are retrigger candidates.
            close = sum(1 for a, b in zip(events, events[1:]) if b['frame_end'] - a['frame_end'] < 112)
            entry['events_within_10ms'] = close
        elif label and events:
            entry['label_agreement'] = round(sum(e['zone'] == label for e in events) / len(events), 3)
        result[name] = entry
    frames.unlink()
    return result


def run(root, output, old_tool, new_tool, pads, settings_text):
    report = {'format': 'open-mimic-trigger-policy-study-v1', 'strong_peak': STRONG_PEAK,
              'scope': 'Mechanism comparison on guided takes; not independent strike truth.', 'pads': {}}
    for pad in sorted(p.name for p in root.iterdir() if p.is_dir() and not p.name.startswith('.')):
        if pads and pad not in pads:
            continue
        sessions = []
        for directory in sorted(p for p in (root / pad).iterdir() if p.is_dir()):
            try:
                records, _ = verified_session(directory)
                fit_session(directory)
            except Exception:
                continue
            sessions.append((directory, records))
        if len(sessions) < 2:
            continue
        report['pads'][pad] = []
        for index, (directory, records) in enumerate(sessions):
            calibration_dir = sessions[index - 1][0]
            profile = fit_session(calibration_dir)
            tools = {'old': (old_tool, legacy_profile(profile), None)} if old_tool else {}
            tools['new'] = (new_tool, native_profile(profile), settings_text)
            with tempfile.TemporaryDirectory() as temporary:
                takes = [study_take(records[phase], directory, profile, tools, settings_text, Path(temporary))
                         for phase in ('head-varied', 'rim-varied', 'rimshot-varied') if phase in records]
            report['pads'][pad].append({'session': directory.name, 'calibration_session': calibration_dir.name,
                                        'zone_rule': profile.get('zone_rule'), 'takes': takes})
            print(f'{pad} {directory.name[:8]} (profile {calibration_dir.name[:8]})', flush=True)
            for take in takes:
                line = f"  {take['phase']:16} isolated {take['isolated_strong_hits']:4}"
                for name in tools:
                    entry = take[name]
                    line += f" | {name}: events {entry['events']:4} zones {entry['zones']}"
                    if 'label_agreement_strong' in entry:
                        line += f" agree {entry['label_agreement_strong']:.2f} <10ms {entry['events_within_10ms']}"
                print(line, flush=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--measurements', type=Path, default=Path(__file__).resolve().parent.parent / 'measurements')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--old-tool', type=Path, help='Pre-study replay executable (quiet-rearm policy)')
    parser.add_argument('--new-tool', type=Path, required=True)
    parser.add_argument('--settings', type=Path, help='Trigger settings file applied to the new policy')
    parser.add_argument('--pads', nargs='*')
    args = parser.parse_args()
    settings_text = args.settings.read_text() if args.settings else None
    run(args.measurements, args.output, args.old_tool, args.new_tool, args.pads, settings_text)


if __name__ == '__main__':
    main()
