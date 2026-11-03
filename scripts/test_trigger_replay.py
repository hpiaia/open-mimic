#!/usr/bin/env python3
"""Exercise the actual C++ trigger engine in offline feature-only replay."""
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

TOOL = os.environ.get('TRIGGER_REPLAY_TOOL')


@unittest.skipUnless(TOOL, 'Set TRIGGER_REPLAY_TOOL to the built C++ executable')
class TriggerReplayTests(unittest.TestCase):
    def run_replay(self, contents, success=True):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'frames.txt'
            path.write_text(contents)
            result = subprocess.run([TOOL, '-', '0', '1', '2', '-', str(path)], text=True, capture_output=True)
            self.assertEqual(result.returncode == 0, success, result.stderr)
            return [json.loads(line) for line in result.stdout.splitlines()]

    def test_real_engine_window_and_gap_reset(self):
        lines = []
        for frame in range(800):
            if frame == 110:
                lines.append('-1')
            peak = round(800 * math.exp(-(frame - 100) / 8)) if 100 <= frame < 180 else 0
            if 500 <= frame < 580:
                peak = round(800 * math.exp(-(frame - 500) / 8))
            lines.append(f'{frame} {2 + peak} 2')
        output = self.run_replay('\n'.join(lines) + '\n')
        # A head-only strike is unambiguous: the rule emits it without the classifier.
        events = [row for row in output if row['kind'] == 'event']
        self.assertEqual([row for row in output if row['kind'] == 'candidate'], [])
        self.assertEqual([e['frame_end'] for e in events], [531])
        self.assertEqual((events[0]['zone'], events[0]['rule_zone'], events[0]['model_decided']), (1, 1, False))
        self.assertGreater(events[0]['head_peak'], 700)
        self.assertEqual(output[-1]['gap_resets'], 1)
        self.assertEqual(output[-1]['events'], 1)

    def test_ambiguous_window_consults_the_classifier_without_dropping_the_hit(self):
        lines = []
        for frame in range(600):
            peak = round(600 * math.exp(-(frame - 300) / 8)) if 300 <= frame < 380 else 0
            lines.append(f'{frame} {2 + peak} {2 + round(peak * 0.8)}')
        output = self.run_replay('\n'.join(lines) + '\n')
        candidates = [row for row in output if row['kind'] == 'candidate']
        events = [row for row in output if row['kind'] == 'event']
        self.assertEqual(len(candidates), 1)
        self.assertEqual(len(candidates[0]['features']), 69)
        self.assertFalse(candidates[0]['valid'])
        self.assertEqual(len(events), 1)
        self.assertFalse(events[0]['model_decided'])
        self.assertIn(events[0]['zone'], (1, 2, 3))

    def test_trigger_settings_file_changes_the_policy(self):
        lines = []
        for frame in range(1200):
            peak = 0
            for start in (300, 400):
                if start <= frame < start + 300:
                    peak += round(900 * math.exp(-(frame - start) / 60))
            lines.append(f'{frame} {2 + peak} 2')
        contents = '\n'.join(lines) + '\n'
        with tempfile.TemporaryDirectory() as temporary:
            frames = Path(temporary) / 'frames.txt'
            frames.write_text(contents)
            strict = Path(temporary) / 'strict.trigger'
            strict.write_text('OPEN_MIMIC_TRIGGER 1\nmask 20\nretrigger 100\n')
            default = subprocess.run([TOOL, '-', '0', '1', '2', '-', str(frames)], text=True, capture_output=True, check=True)
            gated = subprocess.run([TOOL, '-', '0', '1', '2', '-', str(frames), str(strict)], text=True, capture_output=True, check=True)
            self.assertEqual(json.loads(default.stdout.splitlines()[-1])['events'], 2)
            self.assertEqual(json.loads(gated.stdout.splitlines()[-1])['events'], 1)
            bad = Path(temporary) / 'bad.trigger'
            bad.write_text('OPEN_MIMIC_TRIGGER 1\nmask 999\n')
            self.assertNotEqual(subprocess.run([TOOL, '-', '0', '1', '2', '-', str(frames), str(bad)], capture_output=True).returncode, 0)

    def test_invalid_or_unordered_samples_are_rejected(self):
        for contents in ('0 4096 2\n', '1 2 2\n0 2 2\n', '-1 2 3\n', ''):
            self.run_replay(contents, success=False)

    def test_verified_omseq_adapter_reaches_actual_engine(self):
        from contextlib import redirect_stdout
        import io
        from replay_pad_session import replay
        from test_rerun_pad_measurements import session_fixture
        with tempfile.TemporaryDirectory() as temporary, redirect_stdout(io.StringIO()):
            root = Path(temporary)
            source = session_fixture(root)
            report = replay(source, root / 'replay', Path(TOOL))
            self.assertEqual(len(report['phases']), 4)
            self.assertEqual(next(r for r in report['phases'] if r['phase'] == 'ambient')['summary']['events'], 0)
            self.assertGreater(next(r for r in report['phases'] if r['phase'] == 'head-varied')['summary']['events'], 0)


if __name__ == '__main__':
    unittest.main()
