#!/usr/bin/env python3
"""Verify response application ordering around the durable archive acknowledgment."""
import json
from pathlib import Path
import tempfile
import unittest

from receive_pad_measurements import CalibrationRejected, Receiver
from test_pad_response import session_fixture


class ResponseReceiverTests(unittest.TestCase):
    def test_response_is_applied_before_sd_acknowledgment(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = session_fixture(root)
            report = json.loads((folder / "download.json").read_text())
            receiver = Receiver("root@module", root)
            calls = []
            receiver.response_result = lambda *args, **kwargs: calls.append(("profile", kwargs["failed"]))
            receiver.command = lambda *args: calls.append((args[0], False))
            receiver.remote = lambda *args, **kwargs: "ack"
            receiver.acknowledge("tom1", "a" * 32, folder, report)
            self.assertEqual(calls, [("profile", False), ("archive-ack", False)])
            self.assertEqual(report["response_calibration"]["state"], "applied")
            profile = json.loads((root / ".profiles/tom1.json").read_text())
            self.assertEqual(profile["session"], "a" * 32)

    def test_insufficient_data_retains_old_profile_and_still_archives(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = session_fixture(root, flat=True)
            output = root / ".profiles"
            output.mkdir()
            previous = output / "tom1.json"
            previous.write_text('{"previous": true}')
            report = json.loads((folder / "download.json").read_text())
            receiver = Receiver("root@module", root)
            calls = []
            receiver.response_result = lambda *args, **kwargs: calls.append(("profile", kwargs["failed"]))
            receiver.command = lambda *args: calls.append((args[0], False))
            receiver.remote = lambda *args, **kwargs: "ack"
            receiver.acknowledge("tom1", "a" * 32, folder, report)
            self.assertEqual(calls, [("profile", True), ("archive-ack", False)])
            self.assertEqual(previous.read_text(), '{"previous": true}')
            self.assertEqual(report["response_calibration"]["state"], "unavailable")
            self.assertTrue(report["device_captures_removed"])

    def test_corruption_is_not_treated_as_insufficient_calibration(self):
        from calibrate_pad import RecordingIntegrityError
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = session_fixture(root)
            payload = next(folder.glob("*.omseq"))
            with payload.open("ab") as stream:
                stream.write(b"changed after archival")
            receiver = Receiver("root@module", root)
            receiver.command = lambda *args: self.fail("Corrupted local archive released the SD source")
            receiver.response_result = lambda *args, **kwargs: self.fail("Corruption was reported as a response-quality issue")
            report = json.loads((folder / "download.json").read_text())
            with self.assertRaises(RecordingIntegrityError):
                receiver.acknowledge("tom1", "a" * 32, folder, report)

    def test_device_rejection_preserves_the_recording(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = session_fixture(root)
            receiver = Receiver("root@module", root)
            calls = []
            def reject(*args, **kwargs):
                raise CalibrationRejected("Sensor type changed; record a new calibration")
            receiver.response_result = reject
            receiver.command = lambda *args: calls.append(args[0])
            receiver.remote = lambda *args, **kwargs: "ack"
            report = json.loads((folder / "download.json").read_text())
            receiver.acknowledge("tom1", "a" * 32, folder, report)
            self.assertEqual(calls, ["archive-ack"])
            self.assertEqual(report["response_calibration"]["state"], "unavailable")
            self.assertTrue(report["device_captures_removed"])

    def test_transport_failure_does_not_release_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = session_fixture(root)
            receiver = Receiver("root@module", root)
            receiver.command = lambda *args: self.fail("Unconfirmed profile released the SD source")
            def offline(*args, **kwargs):
                raise TimeoutError("Disconnected")
            receiver.response_result = offline
            report = json.loads((folder / "download.json").read_text())
            with self.assertRaises(TimeoutError):
                receiver.acknowledge("tom1", "a" * 32, folder, report)
            self.assertFalse(report.get("device_captures_removed", False))


if __name__ == "__main__":
    unittest.main()
