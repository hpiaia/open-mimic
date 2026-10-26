#!/usr/bin/env python3
"""Replay verified OMSEQ recordings through the actual C++ trigger engine.

Runs offline and opens no hardware or MIDI devices. Default mode records feature
vectors without a classifier. A LiteRT-enabled replay executable can also take
--model. Response profiles may use the same session; this is mechanism analysis,
not independent classification accuracy or a physical strike count.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from calibrate_pad import global_channel, native_profile, verified_session
from inspect_pad_sequence import read_records


def replay(directory, output, tool, profile=None, model=None, keep_frames=False):
    if output.exists():
        raise ValueError('Choose a fresh replay output directory')
    records, fingerprint = verified_session(directory)
    if not records:
        raise ValueError('No verified continuous recordings')
    output.mkdir(parents=True)
    profile_path = '-'
    response = json.loads(profile.read_text()) if profile else None
    if response:
        profile_path = str((output / 'response.profile').resolve())
        Path(profile_path).write_text(native_profile(response))
    results = []
    for phase, (path, metadata) in sorted(records.items()):
        head = global_channel(metadata['head'])
        rim = global_channel(metadata.get('input_rim', metadata.get('rim')))
        zones = metadata['zone_count']
        if (head // 16 != rim // 16 or head == rim or zones not in (1, 2)):
            raise ValueError('Unsupported physical sensor configuration')
        if response and (response['pad'], response['head'], response['rim'], response['zones']) != (
                metadata['pad'], head, rim, zones):
            raise ValueError('Response profile differs from the captured pad/sensors')
        if phase not in ('ambient', 'head-varied', 'rim-varied', 'rimshot-varied'):
            raise ValueError('Unsupported guided phase')
        print(f'Replaying {metadata["pad"]} / {metadata["session_id"][:8]} / {phase}', flush=True)
        frames = output / f'{phase}.frames.txt'
        position = 0
        with frames.open('w') as stream:
            for _, batches, flags, _ in read_records(path.parent / metadata['capture']):
                if flags:
                    stream.write('-1\n')
                    position += 1
                    continue
                for raw in batches[head // 16]:
                    channels = {word >> 12: word & 4095 for word in raw}
                    stream.write(f'{position} {channels[head % 16]} {channels[rim % 16] if zones == 2 else 0}\n')
                    position += 1
        target = output / f'{phase}.jsonl'
        with target.open('w') as stream:
            subprocess.run([str(tool.resolve()), str(model.resolve()) if model else '-',
                            str(head), str(rim), str(zones), profile_path, str(frames.resolve())],
                           stdout=stream, check=True, timeout=120)
        with target.open() as stream:
            last = None
            for line in stream:
                last = json.loads(line)
        if not last or last.get('kind') != 'summary':
            raise ValueError('Replay did not produce a complete summary')
        results.append({'phase': phase, 'capture': metadata['capture'], 'summary': last})
        if not keep_frames:
            frames.unlink()
    _, after = verified_session(directory)
    if after != fingerprint:
        raise ValueError('Source session changed during replay')
    report = {'format': 'open-mimic-trigger-replay-v1', 'source_hash': fingerprint,
              'tool_sha256': hashlib.sha256(tool.read_bytes()).hexdigest(),
              'response_session': response['session'] if response else None,
              'model_sha256': hashlib.sha256(model.read_bytes()).hexdigest() if model else None,
              'scope': 'Actual C++ event policy. Guided tasks are not independent strike timestamps; no physical latency or event recall claim.',
              'phases': results}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=Path(__file__).resolve().parent.parent / 'build/mimic-trigger-replay')
    parser.add_argument('--profile', type=Path, help='Per-pad response JSON; otherwise use uncalibrated policy')
    parser.add_argument('--model', type=Path, help='Requires a LiteRT-enabled replay tool')
    parser.add_argument('--keep-frames', action='store_true')
    args = parser.parse_args()
    try:
        report = replay(args.session, args.output_dir, args.tool, args.profile, args.model, args.keep_frames)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, f'Replay failed: {error}\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
