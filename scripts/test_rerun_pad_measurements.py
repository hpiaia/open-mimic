#!/usr/bin/env python3
"""Verify physical channel decoding, gap isolation and verified Rerun exports."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from calibrate_pad import RecordingIntegrityError
from inspect_pad_sequence import FRAME, HEADER, RECORD, read_records
from rerun_pad_measurements import export_phase, export_session, read_sensors, select_sessions
from test_sequence_training import fixture


def session_fixture(root, session='b' * 32, utc='2026-10-26T12:00:00+00:00', gap=False):
    directory = root / 'tom1' / session
    directory.mkdir(parents=True)
    for index, phase in enumerate(('ambient', 'head-varied', 'rim-varied', 'rimshot-varied')):
        with tempfile.TemporaryDirectory() as temporary:
            source = fixture(Path(temporary), phase=phase, gap=gap and phase != 'ambient')
            metadata = json.loads(source.read_text())
            metadata['session_id'] = session
            name = f'{index:032x}'
            (source.parent / metadata['capture']).rename(directory / (name + '.omseq'))
            metadata['capture'] = name + '.omseq'
            (directory / (name + '.json')).write_text(json.dumps(metadata))
    files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in directory.iterdir()}
    (directory / 'download.json').write_text(json.dumps({'utc': utc, 'verified_files': files}))
    return directory


class RerunExportTests(unittest.TestCase):
    def test_tagged_channels_and_gap_boundaries(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = fixture(Path(temporary), gap=True)
            metadata = json.loads(path.read_text())
            segments, gaps, rate = read_sensors(path.parent / metadata['capture'], metadata)
            self.assertEqual(len(segments), 2)
            self.assertEqual(len(gaps), 1)
            self.assertEqual(segments[0]['values'][100].tolist(), [702, 352])
            self.assertEqual(gaps[0]['frame'], 70 * 16)
            self.assertEqual(segments[1]['start'], gaps[0]['frame'] + 1)
            self.assertEqual(sum(len(s['values']) for s in segments), 159 * 16)
            self.assertGreater(rate, 1000)
            metadata['zone_count'] = 1
            mono, _, _ = read_sensors(path.parent / metadata['capture'], metadata)
            self.assertTrue(all((segment['values'][:, 1] == 0).all() for segment in mono))

    def test_verified_export_is_read_only_and_cached(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()):
            root = Path(temporary)
            directory = session_fixture(root, gap=True)
            originals = {p.name: p.read_bytes() for p in directory.iterdir()}
            output = root / 'exports'
            target = export_session(directory, output)
            self.assertTrue(target.stat().st_size > 1024)
            report = json.loads(target.with_suffix('.json').read_text())
            self.assertEqual(len(report['phases']), 4)
            self.assertEqual(report['phases'][0]['onset_candidates'], 0)
            self.assertEqual(report['phases'][0]['gap_records'], 0)
            self.assertTrue(all(phase['gap_records'] == 1 for phase in report['phases'][1:]))
            self.assertGreater(report['phases'][1]['onset_candidates'], 0)
            for phase in report['phases'][1:]:
                for candidate in phase['candidates']:
                    self.assertEqual(candidate['frame_in_take'], candidate['frame'] +
                                     (70 * 16 + 1 if candidate['segment'] else 0))
            before = target.stat().st_mtime_ns
            self.assertEqual(export_session(directory, output), target)
            self.assertEqual(target.stat().st_mtime_ns, before)
            self.assertEqual(originals, {p.name: p.read_bytes() for p in directory.iterdir()})

    def test_corrupted_payload_is_never_exported(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = session_fixture(root)
            next(directory.glob('*.omseq')).write_bytes(b'corrupt')
            with self.assertRaises(RecordingIntegrityError):
                export_session(directory, root / 'exports')
            self.assertFalse((root / 'exports').exists())

    def test_latest_is_download_time_not_random_session_id(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            older = session_fixture(root, session='f' * 32)
            newer = session_fixture(root, session='a' * 32, utc='2026-10-26T13:00:00+00:00')
            self.assertEqual(select_sessions(root, 'tom1'), [newer])
            self.assertEqual(select_sessions(root, 'tom1', all_sessions=True), [older, newer])
            self.assertEqual(select_sessions(root, 'tom1', session='f' * 32), [older])
            with self.assertRaises(ValueError):
                select_sessions(root, '../tom1')

    def test_background_windows_are_included_in_visual_mask(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = fixture(Path(temporary), phase='ambient')
            metadata = json.loads(path.read_text())
            sensors = [{'baseline': 2, 'threshold': 2}, {'baseline': 2, 'threshold': 3}]
            masks = []

            def collect(_recording, entity, _frames, values, *_args, **_kwargs):
                if '/windows/' in entity:
                    masks.extend(values)

            import rerun as rr
            recording = rr.RecordingStream('test-background-mask')
            try:
                with patch('rerun_pad_measurements.send_scalar', side_effect=collect):
                    report = export_phase(recording, path, metadata, sensors)
                self.assertGreater(report['background_windows'], 0)
                self.assertIn(.4, masks)
                self.assertNotIn(1, masks)
            finally:
                recording.disconnect()

    def test_quantized_ambient_is_visible_and_has_training_windows(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()):
            root = Path(temporary)
            directory = session_fixture(root)
            capture = directory / ('0' * 32 + '.omseq')
            records = list(read_records(capture))
            with capture.open('wb') as stream:
                stream.write(HEADER.pack(b'OMSEQ001', 1, 2000))
                for timestamp, batches, flags, _ in records:
                    stream.write(RECORD.pack(timestamp, len(batches[0]), len(batches[1]), flags))
                    for adc, batch in enumerate(batches):
                        for index, frame in enumerate(batch):
                            words = [word + int(adc == 1 and index == 0 and word >> 12 == 11) for word in frame]
                            stream.write(FRAME.pack(*words))
            download = directory / 'download.json'
            report = json.loads(download.read_text())
            report['verified_files'][capture.name] = hashlib.sha256(capture.read_bytes()).hexdigest()
            download.write_text(json.dumps(report))
            target = export_session(directory, root / 'exports')
            ambient = json.loads(target.with_suffix('.json').read_text())['phases'][0]
            self.assertGreater(ambient['background_windows'], 0)
            self.assertEqual(ambient['valid_frames'], 2560)
            self.assertEqual(ambient['extraction_error'], '')


if __name__ == '__main__':
    unittest.main()
