#!/usr/bin/env python3
"""Exercise the MIDI service through a Linux PTY in an isolated Docker container.

No MIDI hardware is opened. Run in a container without device passthrough:
  python3 scripts/test_midi_service.py --mimicd build/midi-tests/mimicd
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import sys
import time

STATUS = Path("/run/mimic-midi/status.json")
COMMAND = Path("/run/mimic-midi/command")


def wait_status(predicate, description: str, timeout: float = 4):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            last = json.loads(STATUS.read_text())
            if predicate(last):
                return last
        except (FileNotFoundError, json.JSONDecodeError):
            pass
        time.sleep(0.01)
    raise AssertionError(f"Timed out waiting for {description}: {last}")


def receive_wire(master: int, count: int, timeout: float = 2) -> bytes:
    deadline = time.monotonic() + timeout
    result = bytearray()
    while len(result) < count and time.monotonic() < deadline:
        ready, _, _ = select.select([master], [], [], max(0, deadline - time.monotonic()))
        if ready:
            result.extend(os.read(master, 4096))
    if len(result) != count:
        raise AssertionError(f"Expected {count} UART bytes, received {result.hex()}")
    return bytes(result)


def fifo(line: str):
    descriptor = os.open(COMMAND, os.O_WRONLY | os.O_NONBLOCK)
    try:
        data = (line + "\n").encode()
        assert os.write(descriptor, data) == len(data)
    finally:
        os.close(descriptor)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mimicd", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "linux" or not Path("/.dockerenv").exists():
        parser.error("run inside an isolated Linux Docker container")
    if any(Path("/dev/snd").glob("midi*")):
        parser.error("remove MIDI device passthrough before running this test")
    if COMMAND.exists() or Path("/etc/mimic-midi.conf").exists():
        parser.error("run in a fresh container without existing MIDI service state")
    executable = str(args.mimicd.resolve())
    master, slave = pty.openpty()
    os.set_blocking(master, False)
    process = subprocess.Popen([executable, "midi-service", "--din-device", os.ttyname(slave)],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    checks = []
    try:
        state = wait_status(lambda s: s["running"] and any(p["id"] == "din" and p["connected"]
                                                           for p in s["ports"]), "PTY connection")
        assert state["selectedInput"] == "din" and state["selectedOutput"] == "din"
        assert not select.select([master], [], [], 0.1)[0], "service sent unsolicited MIDI"
        checks.append("startup configures 31250 baud and emits no MIDI")

        os.write(master, bytes([0x99, 36, 127, 38, 0, 0xf8]))
        state = wait_status(lambda s: s["rxMessages"] == 3, "running-status input")
        assert any("note36" in event["text"] for event in state["events"])
        assert any("Note off" in event["text"] for event in state["events"])
        checks.append("DIN input parses running status and interleaved real time")

        os.write(master, bytes([0xf0, 0x7e, 1, 0xfe, 0xf7]))
        wait_status(lambda s: s["rxMessages"] == 5, "SysEx with active sensing")
        checks.append("SysEx survives interleaved active sensing")

        fifo("test 10 36 96 50" + " " * 270)
        state = wait_status(lambda s: bool(s["error"]), "oversized command rejection")
        assert state["txMessages"] == 0, "truncated command prefix was executed"
        checks.append("oversized IPC commands are discarded as complete lines")

        subprocess.run([executable, "midi-config", "--channel", "1"], check=True)
        state = wait_status(lambda s: s["inputChannel"] == 1 and not s["error"], "input channel filter")
        baseline = state["rxMessages"]
        os.write(master, bytes([0x99, 40, 90, 0x90, 41, 91]))
        wait_status(lambda s: s["rxMessages"] == baseline + 1, "filtered MIDI input")
        time.sleep(0.25)
        assert json.loads(STATUS.read_text())["rxMessages"] == baseline + 1
        checks.append("channel filtering preserves only selected channel messages")

        subprocess.run([executable, "midi-test", "--channel", "10", "--note", "36",
                        "--velocity", "96", "--ms", "100"], check=True)
        started = time.monotonic()
        assert receive_wire(master, 3) == bytes([0x99, 36, 96])
        assert receive_wire(master, 3) == bytes([0x89, 36, 0])
        assert time.monotonic() - started >= 0.07, "note off was not scheduled"
        checks.append("manual note test writes exact note-on and scheduled note-off")

        fifo("hit 10 36 90 300")
        assert receive_wire(master, 3) == bytes([0x99, 36, 90])
        time.sleep(0.08)
        fifo("hit 10 36 110 300")
        assert receive_wire(master, 6) == bytes([0x89, 36, 0, 0x99, 36, 110])
        assert not select.select([master], [], [], 0.24)[0], "old timer cut off a retriggered note"
        assert receive_wire(master, 3) == bytes([0x89, 36, 0])
        checks.append("live hits retrigger the same note without an obsolete note-off timer")

        subprocess.run([executable, "midi-test", "--note", "38", "--ms", "5000"], check=True)
        assert receive_wire(master, 3) == bytes([0x99, 38, 96])
        subprocess.run([executable, "midi-config", "--output", "none"], check=True)
        assert receive_wire(master, 3) == bytes([0x89, 38, 0])
        wait_status(lambda s: s["selectedOutput"] == "none", "output switch")
        checks.append("switching outputs releases the service's outstanding note")

        subprocess.run([executable, "midi-config"], check=True)
        wait_status(lambda s: s["selectedOutput"] == "din", "output reconnect")
        subprocess.run([executable, "midi-panic"], check=True)
        expected = bytes(value for channel in range(16)
                         for value in (0xb0 | channel, 123, 0, 0xb0 | channel, 120, 0))
        assert receive_wire(master, 96) == expected
        checks.append("manual panic sends all-notes-off and all-sound-off on 16 channels")

        rejected = subprocess.run([executable, "midi-test", "--note", "128"], capture_output=True)
        assert rejected.returncode != 0
        assert not select.select([master], [], [], 0.05)[0]
        checks.append("invalid output values are rejected before writing bytes")

        subprocess.run([executable, "midi-test", "--note", "42", "--ms", "5000"], check=True)
        assert receive_wire(master, 3) == bytes([0x99, 42, 96])
        process.send_signal(signal.SIGTERM)
        assert receive_wire(master, 3) == bytes([0x89, 42, 0])
        assert process.wait(timeout=3) == 0
        wait_status(lambda s: not s["running"], "clean shutdown")
        checks.append("SIGTERM releases pending notes and marks status stopped")
        print(json.dumps({"kind": "isolated-pty-integration", "checks": checks}, indent=2))
        return 0
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        stdout, stderr = process.communicate()
        if process.returncode:
            print(stdout + stderr, file=sys.stderr)
        os.close(slave)
        os.close(master)


if __name__ == "__main__":
    raise SystemExit(main())
