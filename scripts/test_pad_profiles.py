#!/usr/bin/env python3
"""Exercise response-profile transactions and persistence in an isolated ADC service."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from calibrate_pad import native_profile
from test_pad_service import status_when, send


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mimicd", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "linux" or not Path("/.dockerenv").exists():
        parser.error("use an isolated Linux container")
    project = Path(__file__).resolve().parents[1]
    executable = str(args.mimicd.resolve())
    with tempfile.TemporaryDirectory(prefix="open-mimic-response-") as temporary:
        root = Path(temporary)
        for adc in range(2):
            with (root / f"adc{adc}").open("wb") as output:
                output.truncate(8192)
        (root / "mode").write_text("quiet")
        shim = root / "adc.so"
        subprocess.run(["cc", "-shared", "-fPIC", str(project / "software/tests/fake_adc.c"),
                        "-o", str(shim), "-ldl"], check=True)
        env = dict(os.environ, LD_PRELOAD=str(shim), OPEN_MIMIC_TEST_ADC_DIRECTORY=str(root))
        data = root / "data"
        data.mkdir()
        process = None
        def start():
            return subprocess.Popen([executable, "pad-service", "--data-directory", str(data)],
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        def stop(process):
            process.terminate()
            process.wait(timeout=10)
            assert process.returncode == 0, process.stderr.read()
        def profile(state):
            return next(p for p in state.get("pads", []) if p["id"] == "tom1")["calibration"]
        def transfer(contents, number, failed=False):
            token = f"{number:032x}"
            (data / "profiles/staging" / (token + (".error" if failed else ".profile"))).write_text(contents)
            send(("profile-failed" if failed else "profile-load") + " tom1 " + token)
            return status_when(lambda s: profile(s).get("attempt") == token, "profile acknowledgment")
        value = {"pad": "tom1", "session": "a" * 32, "source_hash": "b" * 64,
                 "head": 26, "rim": 27, "zones": 2,
                 "sensors": [{"noise_mean": .5, "noise_limit": 1, "threshold": 3, "baseline": 2}] * 2,
                 "articulations": {name: {"minimum": 8, "maximum": maximum, "exponent": 1, "samples": 30}
                                   for name, maximum in (("head", 800), ("rim", 500), ("rimshot", 1500))}}
        payload = native_profile(value)
        try:
            process = start()
            status_when(lambda s: s.get("running") and len(s.get("pads", [])) == 11, "startup")
            state = transfer(payload, 1)
            assert profile(state)["state"] == "active" and profile(state)["headThreshold"] == 3
            saved = data / "profiles/tom1.profile"
            original = hashlib.sha256(saved.read_bytes()).hexdigest()
            state = transfer("OPEN_MIMIC_RESPONSE 99\n", 2)
            assert profile(state)["state"] == "active" and profile(state)["error"] and not state["error"]
            assert hashlib.sha256(saved.read_bytes()).hexdigest() == original
            send("zones tom1 1")
            status_when(lambda s: profile(s)["state"] == "stale", "changed sensor count")
            send("zones tom1 2")
            status_when(lambda s: profile(s)["state"] == "active", "sensor count restored")
            stop(process)
            process = start()
            status_when(lambda s: s.get("running") and profile(s)["session"] == "a" * 32, "persistent response")
            state = transfer(payload, 3)
            assert profile(state)["error"] == "" and hashlib.sha256(saved.read_bytes()).hexdigest() == original
            state = transfer("Only 3 usable head hits; record at least 8", 4, failed=True)
            assert profile(state)["state"] == "active" and "Only 3" in profile(state)["error"]
            send("profile-reset tom1")
            status_when(lambda s: profile(s)["state"] == "none" and not profile(s)["error"], "reset")
            assert not saved.exists()
            wrong = dict(value, head=1, rim=2)
            state = transfer(native_profile(wrong), 5)
            assert profile(state)["state"] == "none" and "physical input" in profile(state)["error"]
            state = transfer("x" * 9000, 6)
            assert "safe size" in profile(state)["error"] and not saved.exists()
            print("PASS: response transfer, invalid replacement rollback, sensor mismatch, restart persistence, retry and reset")
        finally:
            if process is not None and process.poll() is None:
                stop(process)


if __name__ == "__main__":
    main()
