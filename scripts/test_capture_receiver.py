#!/usr/bin/env python3
"""Check archive durability boundaries and failure behavior without SSH or physical captures."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from receive_pad_measurements import Receiver, digest, verified_files


class ReceiverTests(unittest.TestCase):
    def test_verify_rejects_corruption_and_symlinks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            file = root / "ready.json"
            file.write_text("original")
            expected = {file.name: digest(file)}
            verified_files(root, expected)
            file.write_text("changed")
            with self.assertRaises(ValueError):
                verified_files(root, expected)
            file.unlink()
            file.symlink_to("/etc/hosts")
            with self.assertRaises(ValueError):
                verified_files(root, expected)

    def test_verified_archive_precedes_ack_and_can_retry(self):
        for corrupt in (False, True):
            with self.subTest(corrupt=corrupt), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                source = root / "source"
                source.mkdir()
                (source / "ready.json").write_text('{}')
                (source / ("a" * 32 + ".omadc")).write_bytes(b"waveform")
                expected = {p.name: digest(p) for p in source.iterdir()}
                output = root / "measurements"
                output.mkdir()
                pad, session = "tom1", "b" * 32
                destination = output / pad / session
                acknowledged = []
                receiver = Receiver("root@module", output)
                receiver.checksums = lambda path: expected.copy()
                receiver.remote = lambda command, **kwargs: "ack"
                def ack(action, actual_pad, actual_session):
                    self.assertEqual((action, actual_pad, actual_session), ("archive-ack", pad, session))
                    self.assertTrue((destination / "download.json").is_file())
                    verified_files(destination, expected)
                    acknowledged.append(True)
                receiver.command = ack
                def copy(command, **kwargs):
                    target = Path(command[-1])
                    shutil.copytree(source, target)
                    if corrupt:
                        (target / ("a" * 32 + ".omadc")).write_bytes(b"damaged")
                    return subprocess.CompletedProcess(command, 0)
                with patch("receive_pad_measurements.subprocess.run", side_effect=copy):
                    if corrupt:
                        with self.assertRaises(ValueError):
                            receiver.archive(pad, session)
                        self.assertFalse(acknowledged)
                        self.assertFalse(destination.exists())
                    else:
                        receiver.archive(pad, session)
                        self.assertTrue(json.loads((destination / "download.json").read_text())["device_captures_removed"])
                        receiver.archive(pad, session)
                        self.assertEqual(len(acknowledged), 2)

    def test_ack_recovers_after_source_deletion(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pad, session = "tom2", "c" * 32
            destination = root / pad / session
            destination.mkdir(parents=True)
            (destination / "ready.json").write_text('{}')
            hashes = {"ready.json": digest(destination / "ready.json")}
            (destination / "download.json").write_text(json.dumps({"verified_files": hashes, "device_captures_removed": False}))
            receiver = Receiver("root@module", root)
            calls = []
            receiver.remote = lambda command, **kwargs: "absent" if command.startswith("test -d") else "ack"
            receiver.command = lambda *args: calls.append(args)
            receiver.checksums = lambda path: self.fail("deleted source was read")
            receiver.archive(pad, session)
            self.assertEqual(calls, [("archive-ack", pad, session)])
            self.assertTrue(json.loads((destination / "download.json").read_text())["device_captures_removed"])

    def test_changed_source_is_never_acknowledged(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "measurements"
            output.mkdir()
            receiver = Receiver("root@module", output)
            receiver.command = lambda *args: self.fail("changed source was acknowledged")
            records = iter([{"ready.json": "a" * 64}, {"ready.json": "b" * 64}])
            receiver.checksums = lambda path: next(records)
            def copy(command, **kwargs):
                Path(command[-1]).mkdir()
                return subprocess.CompletedProcess(command, 0)
            with patch("receive_pad_measurements.subprocess.run", side_effect=copy):
                with self.assertRaisesRegex(ValueError, "source changed"):
                    receiver.archive("tom1", "a" * 32)


if __name__ == "__main__":
    unittest.main()
