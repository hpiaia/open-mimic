#!/usr/bin/env python3
"""Exercise fixed routing, guided capture, continuous takes and archive acknowledgment in isolated Linux."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

STATUS = Path("/run/mimic-pads/status.json")
COMMAND = Path("/run/mimic-pads/command")


def status_when(predicate, description, timeout=15):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            last = json.loads(STATUS.read_text())
            if predicate(last):
                return last
        except (FileNotFoundError, json.JSONDecodeError):
            pass
        time.sleep(0.02)
    raise AssertionError(f"Timed out waiting for {description}: {last}")


def send(line):
    descriptor = os.open(COMMAND, os.O_WRONLY | os.O_NONBLOCK)
    try:
        payload = (line + "\n").encode()
        assert os.write(descriptor, payload) == len(payload)
    finally:
        os.close(descriptor)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full-wizard", action="store_true")
    parser.add_argument("--mimicd", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "linux" or not Path("/.dockerenv").exists() or any(Path("/dev").glob("ads7953.*")):
        parser.error("run in a fresh isolated Linux container without ADC passthrough")
    project = Path(__file__).resolve().parents[1]
    checks = []
    process = None
    with tempfile.TemporaryDirectory(prefix="open-mimic-pad-test-") as temporary:
        root = Path(temporary)
        for adc in range(2):
            with (root / f"adc{adc}").open("wb") as stream:
                stream.truncate(8192)
        mode = root / "mode"
        mode.write_text("quiet")
        shim = root / "fake-adc.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        str(project / "software/tests/fake_adc.c"), "-o", str(shim), "-ldl"], check=True)
        env = dict(os.environ, LD_PRELOAD=str(shim), OPEN_MIMIC_TEST_ADC_DIRECTORY=str(root))
        snapshot = root / "snapshot-test"
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(project / "software/common"), str(project / "software/common/Adc.cpp"),
                        str(project / "software/tests/adc_snapshot_test.cpp"), "-o", str(snapshot)], check=True)
        subprocess.run([str(snapshot)], env=env, check=True)
        checks.append("raw byte preservation and tagged channels survive DMA wrap")
        executable = str(args.mimicd.resolve())
        data = root / "data"
        data.mkdir()
        # A legacy editable map is deliberately wrong: only zones survive migration.
        (data / "config").write_text("OPEN_MIMIC_PADS 2\ntom1 2 31 0 10 12 13\n")
        try:
            process = subprocess.Popen([executable, "pad-service", "--data-directory", str(data)],
                                       env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            state = status_when(lambda s: s.get("running") and len(s.get("pads", [])) == 11, "startup")
            pads = {p["id"]: p for p in state["pads"]}
            assert (pads["tom1"]["head"], pads["tom1"]["rim"]) == (26, 27)
            assert (pads["tom1"]["headNote"], pads["tom1"]["rimNote"], pads["tom1"]["rimshotNote"]) == (47, 79, 80)
            assert (pads["tom2"]["headNote"], pads["tom2"]["rimNote"], pads["tom2"]["rimshotNote"]) == (45, 77, 78)
            assert (pads["aux2"]["headNote"], pads["aux2"]["rimNote"], pads["aux2"]["rimshotNote"]) == (48, 81, 82)
            assert (data / "config").read_text().startswith("OPEN_MIMIC_PADS 3\n")
            checks.append("legacy maps migrate to immutable physical routes and SD3 articulations")
            send("config tom1 2 0 1 10 48 49")
            status_when(lambda s: "Unknown pad command" in s.get("error", ""), "removed ADC edit command")
            send("select tom1")
            status_when(lambda s: s.get("selectedPad") == "tom1" and not s["error"], "selection")
            send("start noise")
            state = status_when(lambda s: s.get("state") == "review" and s.get("captured") == 12, "quiet capture")
            session = state["sessionId"]
            checks.append("twelve background windows recorded without manual progression")
            mode.write_text("head")
            for phase in ("head-medium", "rimshot-medium"):
                send("start " + phase)
                state = status_when(lambda s: s.get("phase") == phase and s.get("state") == "review"
                                    and s.get("captured") == 12, phase)
                metadata = json.loads(Path(state["lastCapture"]).with_suffix(".json").read_text())
                assert metadata["phase"] == phase and metadata["head"] == {"adc": 1, "channel": 10}
                assert metadata["channel_addressing"] == "ads7953-tag-v1" and metadata["zone_count"] == 2
            mode.write_text("quiet")
            checks.append("head and rimshot labels retain tagged raw waveforms and quality")
            before = (data / "config").read_bytes()
            send("settings tom1 2 10 47 79 999")
            status_when(lambda s: bool(s.get("error")), "invalid note rejection")
            assert before == (data / "config").read_bytes()
            send("settings tom1 2 10 47 79 80")
            status_when(lambda s: not s.get("error"), "valid MIDI settings")
            send("archive-ack tom1 " + session)
            status_when(lambda s: "not sealed" in s.get("error", ""), "unsealed deletion rejection")
            send("archive-seal tom1 " + session)
            marker = data / "dataset/tom1" / session / "ready.json"
            status_when(lambda s: marker.exists() and not s.get("error"), "session sealing")
            send("start head-soft")
            status_when(lambda s: "sealed" in s.get("error", ""), "sealed sessions cannot change")
            send("archive-ack tom1 " + session)
            receipt = data / "archive-receipts/tom1" / (session + ".json")
            status_when(lambda s: receipt.exists() and not s.get("error"), "archive acknowledgment")
            assert not marker.parent.exists()
            status_when(lambda s: s.get("uploadState") == "uploaded", "pad-specific archive status")
            checks.append("immutable sealed sessions require acknowledgment before SD release")
            send("new-session")
            status_when(lambda s: s.get("sessionId") != session and not s.get("error"), "new session")
            send("start head-varied")
            state = status_when(lambda s: s.get("phase") == "head-varied" and s.get("state") in ("review", "error")
                                and s.get("writerPending") == 0 and s.get("lastCapture", "").endswith(".omseq"), "continuous twenty-second take", 25)
            metadata = json.loads(Path(state["lastCapture"]).with_suffix(".json").read_text())
            assert metadata["format"] == "open-mimic-pad-sequence-v1"
            raw = Path(state["lastCapture"]).read_bytes()
            assert raw[:8] == b"OMSEQ001" and len(raw) > 100000
            from inspect_pad_sequence import inspect
            sequence = inspect(Path(state["lastCapture"]))
            assert sequence["duration_seconds"] >= 20 and all(count > 1000 for count in sequence["adc_frames"])
            checks.append("rapid playing uses a compact continuous take instead of per-hit dead time")
            send("settings kick 1 10 36 -1 -1")
            status_when(lambda s: not s.get("error"), "single-zone settings")
            send("select kick")
            status_when(lambda s: s.get("selectedPad") == "kick", "single-zone pad")
            send("start rimshot-soft")
            status_when(lambda s: "dual-zone" in s.get("error", ""), "single-zone rimshot rejection")
            send("wizard kick 1")
            state = status_when(lambda s: s.get("wizardActive") and s.get("state") == "countdown", "wizard countdown")
            assert state["wizardSteps"] == 2
            state = status_when(lambda s: s.get("wizardStep") == 1 and s.get("phase") == "head-varied", "automatic stage advancement", 15)
            send("stop")
            status_when(lambda s: not s.get("wizardActive") and s.get("state") == "stopped", "wizard cancellation")
            checks.append("single-zone wizard advances automatically and can be safely stopped")
            if args.full_wizard:
                send("wizard tom1 2")
                status_when(lambda s: s.get("wizardActive") and s.get("selectedPad") == "tom1", "dual wizard")
                deadline = time.monotonic() + 200
                seen = set()
                while time.monotonic() < deadline:
                    state = json.loads(STATUS.read_text())
                    mode.write_text("quiet" if state.get("phase") == "ambient" else "head")
                    seen.add(state.get("phase"))
                    if state.get("state") == "complete":
                        break
                    assert state.get("state") != "error", state
                    time.sleep(0.02)
                else:
                    raise AssertionError("Full wizard did not finish")
                assert {"ambient", "head-varied", "rim-varied", "rimshot-varied"} <= seen
                marker = data / "dataset/tom1" / state["sessionId"] / "ready.json"
                assert marker.is_file() and state["uploadState"] == "pending"
                send("archive-ack tom1 " + state["sessionId"])
                status_when(lambda s: s.get("uploadState") == "uploaded", "wizard archive receipt")
                checks.append("all continuous twenty-second dual-zone wizard stages finish, seal and acknowledge automatically")

        finally:
            if process:
                process.terminate()
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                if process.returncode:
                    print(process.stderr.read(), file=sys.stderr)
    for check in checks:
        print("PASS:", check)
    print(f"{len(checks)} ADC capture, wizard and archive checks passed")


if __name__ == "__main__":
    main()
