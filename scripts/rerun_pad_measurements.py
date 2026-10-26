#!/usr/bin/env python3
"""Export verified pad sessions to the official Rerun viewer (host only).

The source recordings and current training extractor remain unchanged. The frame
axis is local to each independent guided take, not a synchronized wall clock.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid

import numpy as np
import rerun as rr
import rerun.blueprint as rrb

from calibrate_pad import noise_profile, verified_session
from drum_model import FRAMES
from inspect_pad_sequence import read_records
from sequence_training import LABELS, load_sequence_trials
from train_drum_model import channel

ROOT = Path(__file__).resolve().parent.parent
PHASES = ('ambient', 'head-varied', 'rim-varied', 'rimshot-varied')
COLORS = ((80, 170, 255), (255, 163, 72))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_sensors(capture, metadata):
    """Return contiguous segments; reserve one frame-axis slot per unknown gap.

    The reserved slot denotes a discontinuity, not a measured missing duration.
    Poll-end timestamps are retained separately as bounds, never interpolated.
    """
    zones = metadata['zone_count']
    if type(zones) is not int or zones not in (1, 2):
        raise ValueError('Expected one or two active sensors')
    adc, primary = channel(metadata['head'])
    rim_adc, secondary = channel(metadata['rim']) if zones == 2 else (adc, None)
    if adc != rim_adc or primary == secondary:
        raise ValueError('Expected distinct physical sensor channels on the same ADC')
    segments, gaps, chunks, bounds = [], [], [], []
    cursor = segment_start = valid_frames = 0
    elapsed = 0

    def finish():
        if chunks:
            segments.append({'start': segment_start, 'values': np.concatenate(chunks),
                             'poll_end_ns': np.concatenate(bounds)})
            chunks.clear()
            bounds.clear()

    for timestamp, batches, flags, _ in read_records(capture):
        elapsed = timestamp
        if flags:
            finish()
            gaps.append({'frame': cursor, 'poll_end_ns': timestamp})
            cursor += 1
            segment_start = cursor
            continue
        if not batches[adc]:
            continue
        words = np.asarray(batches[adc], dtype=np.uint16)
        tags = words >> 12
        values = np.zeros((len(words), 2), dtype=np.uint16)
        values[:, 0] = words[tags == primary] & 4095
        if zones == 2:
            values[:, 1] = words[tags == secondary] & 4095
        chunks.append(values)
        bounds.append(np.full(len(words), timestamp, dtype=np.int64))
        cursor += len(words)
        valid_frames += len(words)
    finish()
    if valid_frames < FRAMES * 4 or elapsed <= 0:
        raise ValueError('Too few completed sensor frames')
    return segments, gaps, valid_frames * 1e9 / elapsed


def send_scalar(recording, path, frames, values, name, color, points=False, step=False):
    style = (rr.SeriesPoints(names=name, colors=color, marker_sizes=5) if points else
             rr.SeriesLines(names=name, colors=color,
                            aggregation_policy=(rr.components.AggregationPolicy.Off if step else
                                                rr.components.AggregationPolicy.MinMax), interpolation_mode=(
                 rr.components.InterpolationMode.StepAfter if step else rr.components.InterpolationMode.Linear)))
    recording.log(path, style, static=True)
    recording.send_columns(path, indexes=[rr.TimeColumn('frame_in_take', sequence=frames)],
                           columns=rr.Scalars.columns(scalars=values), strict=True)


def export_phase(recording, path, metadata, sensors):
    phase = metadata['phase']
    root = f'takes/{phase}'
    segments, gaps, rate = read_sensors(path.parent / metadata['capture'], metadata)
    try:
        trials = load_sequence_trials(path, sensor_calibration=sensors)
        extraction_error = ''
    except ValueError as error:
        # A valid ambient take may have no windows under the current extractor.
        # Preserve its raw signal so this exclusion can be investigated.
        if str(error) != 'no usable onset or quiet candidates in sequence':
            raise
        trials, extraction_error = [], str(error)
    zones = metadata['zone_count']
    baseline = np.asarray([sensor['baseline'] for sensor in sensors])
    diagnostics = []
    flags = {'near_threshold': 0, 'larger_peak_after_window': 0}
    for index, segment in enumerate(segments):
        raw = segment['values']
        frames = np.arange(segment['start'], segment['start'] + len(raw), dtype=np.int64)
        recording.send_columns(f'{root}/poll_bounds/segment_{index:04d}',
                               indexes=[rr.TimeColumn('frame_in_take', sequence=frames)],
                               columns=rr.AnyValues.columns(poll_end_ns=segment['poll_end_ns']), strict=True)
        for sensor in range(zones):
            name = ('Head', 'Rim')[sensor]
            prefix = f'{root}/signals/segment_{index:04d}/{name.lower()}'
            send_scalar(recording, prefix, frames, raw[:, sensor].astype(float) - baseline[sensor],
                        name, COLORS[sensor])
            for sign, suffix in ((1, 'positive'), (-1, 'negative')):
                send_scalar(recording, f'{root}/thresholds/{name.lower()}/{index}/{suffix}',
                            frames[[0, -1]], [sign * sensors[sensor]['threshold']] * 2,
                            f'{name} threshold {"+" if sign > 0 else "−"}', (140, 140, 140))
        selected = [trial for trial in trials if trial['segment_index'] == index]
        mask = np.zeros(len(raw), dtype=float)
        starts, peaks = [], []
        for trial in selected:
            start = trial['source_frame_start']
            if start < 0 or start + FRAMES > len(raw):
                raise ValueError('Extractor window escaped its contiguous sensor segment')
            if not trial['label']:
                mask[start:start + FRAMES] = np.maximum(mask[start:start + FRAMES], .4)
                recording.set_time('frame_in_take', sequence=segment['start'] + start)
                recording.log(f'{root}/candidates', rr.TextLog(json.dumps({
                    'example_id': trial['example_id'], 'label': 0, 'segment': index,
                    'frame': start, 'frame_in_take': segment['start'] + start})), strict=True)
                continue
            mask[start:start + FRAMES] = 1
            starts.append(segment['start'] + start)
            # Reconstruct the exact extractor baseline from its centered window.
            window_baseline = raw[start].astype(float) - np.asarray(trial['waveform'])[:, 0]
            window = np.abs(raw[start:start + FRAMES, :zones].astype(float) - window_baseline[:zones])
            context = np.abs(raw[start:start + 256, :zones].astype(float) - window_baseline[:zones])
            peak = float(window.max())
            peaks.append(peak)
            near = float((window / [s['threshold'] for s in sensors[:zones]]).max()) <= 2
            late = int(np.argmax(context.max(axis=1))) >= FRAMES and float(context.max()) > 2 * max(1, peak)
            flags['near_threshold'] += near
            flags['larger_peak_after_window'] += late
            detail = {'example_id': trial['example_id'], 'segment': index, 'frame': start,
                      'frame_in_take': segment['start'] + start, 'label': trial['label'],
                      'peak': peak, 'near_threshold': bool(near), 'larger_peak_after_window': bool(late)}
            diagnostics.append(detail)
            recording.set_time('frame_in_take', sequence=segment['start'] + start)
            recording.log(f'{root}/candidates', rr.TextLog(json.dumps(detail)), strict=True)
        # Preserve exact window edges without logging one row per sample for the mask.
        changes = np.unique(np.concatenate(([0], np.flatnonzero(np.diff(mask)) + 1, [len(mask) - 1])))
        send_scalar(recording, f'{root}/windows/{index}', frames[changes], mask[changes],
                    '32-frame window: hit 1 / background 0.4', (184, 127, 255), step=True)
        if starts:
            send_scalar(recording, f'{root}/onsets/{index}', starts, peaks,
                        'Guided onset candidate', (129, 215, 142), points=True)
    for gap in gaps:
        recording.set_time('frame_in_take', sequence=gap['frame'])
        recording.log(f'{root}/gaps', rr.TextLog(
            f"Acquisition gap; missing duration unknown. Poll-end bound: {gap['poll_end_ns']} ns."), strict=True)
    return {'phase': phase, 'capture': metadata['capture'], 'capture_sha256': digest(path.parent / metadata['capture']),
            'estimated_frames_per_second': rate, 'valid_frames': sum(len(s['values']) for s in segments),
            'gap_records': len(gaps), 'axis_frames': sum(len(s['values']) for s in segments) + len(gaps),
            'onset_candidates': len(diagnostics),
            'background_windows': sum(trial['label'] == 0 for trial in trials),
            'extraction_error': extraction_error, 'diagnostic_flags': flags, 'candidates': diagnostics}


def blueprint(phases):
    tabs = []
    for summary in phases:
        phase = summary['phase']
        root = f'takes/{phase}'
        axis = rrb.TimeAxis(view_range=rrb.TimeRange(
            rrb.TimeRangeBoundary.absolute(seq=0),
            rrb.TimeRangeBoundary.absolute(seq=summary['axis_frames'])))
        tabs.append(rrb.Vertical(
            rrb.TimeSeriesView(name='Centered sensor signals and thresholds',
                               contents=[f'{root}/signals/**', f'{root}/thresholds/**', f'{root}/onsets/**'], axis_x=axis),
            rrb.TimeSeriesView(name='Training windows: hit 1 / background 0.4 / outside 0', contents=f'{root}/windows/**',
                               axis_y=rrb.ScalarAxis(range=(-.1, 1.2)), axis_x=axis),
            rrb.TextLogView(name='Candidate provenance and acquisition gaps', contents=[f'{root}/candidates', f'{root}/gaps']),
            row_shares=[.65, .15, .2], name=phase))
    return rrb.Blueprint(rrb.Horizontal(
        rrb.TextDocumentView(name='Session and inspection notes', origin='notes'),
        rrb.Tabs(*tabs, active_tab=1 if len(tabs) > 1 else 0), column_shares=[.25, .75]),
        rrb.TimePanel(timeline='frame_in_take', play_state='paused', expanded=False),
        rrb.SelectionPanel(expanded=False), rrb.BlueprintPanel(expanded=False),
        auto_layout=False, auto_views=False)


def export_session(directory, output):
    records, fingerprint = verified_session(directory)
    if 'ambient' not in records:
        raise ValueError('Session has no verified ambient recording')
    ambient_path, ambient = records['ambient']
    sensors = noise_profile(ambient_path, ambient)['sensors']
    identity = tuple(ambient[k] for k in ('pad', 'session_id', 'head', 'rim', 'zone_count'))
    for _, meta in records.values():
        if tuple(meta[k] for k in ('pad', 'session_id', 'head', 'rim', 'zone_count')) != identity:
            raise ValueError('Session recordings have inconsistent identity or sensors')
        if meta['phase'] not in PHASES:
            raise ValueError('Unsupported guided recording phase')
    pad, session = ambient['pad'], ambient['session_id']
    if not re.fullmatch(r'[a-z][a-z0-9]*', pad) or not re.fullmatch(r'[0-9a-f]{32}', session):
        raise ValueError('Invalid pad or session identifier')
    target = output / pad / f'{session}.rrd'
    sidecar = target.with_suffix('.json')
    versions = {name: digest(Path(__file__).with_name(name)) for name in
                ('rerun_pad_measurements.py', 'sequence_training.py', 'calibrate_pad.py', 'inspect_pad_sequence.py', 'drum_model.py')}
    signature = {'source_hash': fingerprint, 'tool_hashes': versions, 'rerun': rr.__version__}
    if target.exists() and sidecar.exists():
        previous = json.loads(sidecar.read_text())
        if previous['signature'] == signature and previous['rrd_sha256'] == digest(target):
            print(f'Already verified: {target}', flush=True)
            return target
        raise ValueError(f'Output differs from current sources/tools: {target}. Choose a fresh output directory.')
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_suffix('.partial')
    ordered = [phase for phase in PHASES if phase in records]
    # Different extractor/tool versions of one source are separate recordings;
    # the viewer must not merge stale windows from an earlier derivation.
    recording_id = uuid.uuid5(uuid.NAMESPACE_URL, json.dumps(signature, sort_keys=True))
    recording = rr.RecordingStream('Open Mimic Pad Inspection', recording_id=recording_id)
    summaries = []
    try:
        recording.save(partial)
        for phase in ordered:
            print(f'Exporting {pad} / {session[:8]} / {phase}', flush=True)
            summaries.append(export_phase(recording, *records[phase], sensors))
        notes = [f'# {pad} · {session[:8]}', '', '## Read this first',
                 '- Select a guided take using the tabs.',
                 '- Blue: head. Orange: rim. Gray: sensor thresholds. Green: onset candidates.',
                 '- The lower signal marks exact training windows: hit = 1, background = 0.4, outside = 0.',
                 '- Zoom the plot to inspect individual samples. Both sensors share the same amplitude scale.',
                 '- The frame axis is local to each take. Different tabs are independent recordings.',
                 '- Separate line entities prevent interpolation across acquisition gaps.',
                 '- Each gap reserves one axis slot; that slot is not its missing duration.',
                 '- Candidate labels come from the guided task, not independent strike verification.',
                 '- The samples are centered using ambient medians. The extractor may use a slightly different segment baseline.',
                 '- Flags are inspection heuristics, not corrected labels.', '',
                 f'Estimated cadence: {summaries[-1]["estimated_frames_per_second"]:.0f} frames/s. ',
                 'Cadence and window duration are estimates; ADC synchronization is not established.', '',
                 '## Physical sensor map', '```json', json.dumps({k: ambient[k] for k in ('head', 'rim', 'zone_count')}, indent=2),
                 '```', '## Extraction summary']
        for summary in summaries:
            notes.append(f'- **{summary["phase"]}**: {summary["onset_candidates"]} onset candidates, '
                         f'{summary["background_windows"]} background windows, {summary["gap_records"]} acquisition gaps.')
            if summary['extraction_error']:
                notes.append('  - Current extractor: ' + summary['extraction_error'])
        recording.log('notes', rr.TextDocument('\n'.join(notes), media_type='text/markdown'), static=True, strict=True)
        recording.send_blueprint(blueprint(summaries))
        recording.flush()
    finally:
        recording.disconnect()
    # Detect sources changing during export, before publishing the derived recording.
    _, after = verified_session(directory)
    if after != fingerprint:
        raise ValueError('Source session changed during export')
    partial.rename(target)
    report = {'format': 'open-mimic-rerun-export-v1', 'pad': pad, 'session': session,
              'signature': signature, 'rrd_sha256': digest(target), 'sensors': sensors, 'phases': summaries}
    temporary = sidecar.with_suffix('.json.partial')
    temporary.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    temporary.rename(sidecar)
    return target


def select_sessions(root, pad=None, session='latest', all_sessions=False):
    if pad is not None and not re.fullmatch(r'[a-z][a-z0-9]*', pad):
        raise ValueError('Invalid pad name')
    if session != 'latest' and not re.fullmatch(r'[0-9a-f]{32}', session):
        raise ValueError('Invalid session ID')
    selected = []
    pads = [root / pad] if pad else sorted(p for p in root.iterdir() if p.is_dir() and not p.name.startswith('.'))
    for directory in pads:
        candidates = list(directory.glob('*/download.json')) if directory.exists() else []
        if session != 'latest':
            candidates = [p for p in candidates if p.parent.name == session]
        candidates.sort(key=lambda p: (json.loads(p.read_text())['utc'], p.parent.name))
        selected.extend(p.parent for p in (candidates if all_sessions else candidates[-1:]))
    if not selected:
        raise ValueError('No verified sessions match the selection')
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--measurements', type=Path, default=ROOT / 'measurements')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'build/pad-inspection')
    parser.add_argument('--pad', help='Select a pad, e.g. tom1 or snare')
    parser.add_argument('--session', default='latest', help='Session ID; default: latest verified download')
    parser.add_argument('--all-sessions', action='store_true')
    parser.add_argument('--open', action='store_true', help='Open one exported session in the native Rerun viewer')
    args = parser.parse_args()
    if args.all_sessions and args.session != 'latest':
        parser.error('--all-sessions cannot be combined with a specific --session')
    try:
        sessions = select_sessions(args.measurements, args.pad, args.session, args.all_sessions)
        if args.open and len(sessions) != 1:
            parser.error('--open requires a single session; select --pad and optionally --session')
        outputs = [export_session(directory, args.output_dir) for directory in sessions]
        print(f'Ready: {len(outputs)} Rerun recording(s) in {args.output_dir}', flush=True)
        if args.open:
            subprocess.Popen([str(Path(sys.executable).parent / 'rerun'), '--new', str(outputs[0])],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'Inspection failed: {error}\n')


if __name__ == '__main__':
    main()
