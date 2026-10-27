#!/usr/bin/env python3
"""Receive, verify and archive sealed Mimic pad sessions, then release their SD copies."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import threading
import time
import uuid

ROOT = "/var/lib/mimic/pads"
RUNTIME = "/run/mimic-pads"
IDENTITY = re.compile(r"([a-z][a-z0-9]*)/([a-f0-9]{32})")
HOST = re.compile(r"(?:[A-Za-z0-9_][A-Za-z0-9_.-]*@)?[A-Za-z0-9_][A-Za-z0-9_.-]*")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            value.update(block)
    return value.hexdigest()


def flush_directory(path: Path):
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def publish_json(path: Path, value: dict):
    temporary = path.with_name(path.name + ".pending")
    with temporary.open("w") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
    flush_directory(path.parent)


def verified_files(directory: Path, expected: dict[str, str]):
    actual = set()
    for path in directory.iterdir():
        if path.is_symlink() or not path.is_file():
            raise ValueError("unexpected local session entry")
        if path.name == "download.json":
            continue
        actual.add(path.name)
        if path.name not in expected or digest(path) != expected[path.name]:
            raise ValueError(f"local checksum differs: {path.name}")
    if actual != set(expected):
        raise ValueError("local session has missing or unexpected files")


class CalibrationRejected(ValueError):
    pass


class ModelRejected(ValueError):
    pass


class ModelAdaptationUnavailable(ValueError):
    pass


class Receiver:
    def __init__(self, host: str, output: Path, ml_python: Path | None = None, adapt_models: bool = True):
        self.host, self.output = host, output
        self.ml_python, self.adapt_models = ml_python, adapt_models
        self.ssh = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
                    "-o", "ServerAliveInterval=5", "-o", "ServerAliveCountMax=2", host]

    def remote(self, command: str, *, content: bytes | None = None, timeout=120) -> str:
        result = subprocess.run([*self.ssh, command], input=content, capture_output=True,
                                check=True, timeout=timeout)
        return result.stdout.decode()

    def command(self, action: str, pad: str, session: str):
        if not IDENTITY.fullmatch(f"{pad}/{session}"):
            raise ValueError("invalid session identity")
        self.remote(f"mimicd pad-command {action} {pad} {session}", timeout=15)

    def checksums(self, directory: str) -> dict[str, str]:
        records = self.remote(f"find {shlex.quote(directory)} -type f -exec sha256sum {{}} \\;")
        result = {}
        for record in records.splitlines():
            checksum, path = record.split(maxsplit=1)
            relative = str(Path(path).relative_to(directory))
            if (not re.fullmatch(r"[a-f0-9]{64}", checksum)
                    or not re.fullmatch(r"(?:[a-f0-9]{32}\.(?:json|omadc|omseq)|ready\.json)", relative)):
                raise ValueError("invalid session checksum record")
            result[relative] = checksum
        if "ready.json" not in result:
            raise ValueError("session is not sealed")
        return result

    def identities(self, import_existing: bool) -> list[tuple[str, str]]:
        records = self.remote(f"find {ROOT}/dataset -mindepth 2 -maxdepth 2 -type d")
        state = json.loads(self.remote(f"cat {RUNTIME}/status.json"))
        inactive = not state.get("wizardActive") and state.get("state") not in ("capturing", "sequence-writing") and not state.get("writerPending")
        result = []
        for path in records.splitlines():
            relative = str(Path(path).relative_to(f"{ROOT}/dataset"))
            match = IDENTITY.fullmatch(relative)
            if not match:
                continue
            pad, session = match.groups()
            if import_existing and inactive:
                self.command("archive-seal", pad, session)
            marker = self.remote(f"test ! -f {shlex.quote(path)}/ready.json || printf ready")
            if marker == "ready":
                result.append((pad, session))
        return result

    def pending_archives(self):
        for path in self.output.glob("*/*/download.json"):
            pad, session = path.parent.parent.name, path.parent.name
            if not IDENTITY.fullmatch(f"{pad}/{session}"):
                continue
            report = json.loads(path.read_text())
            if not report.get("device_captures_removed"):
                yield pad, session

    def response_result(self, pad: str, contents: str, *, failed: bool, expected=None):
        token = uuid.uuid4().hex
        suffix = "error" if failed else "profile"
        self.remote(f"mkdir -p {ROOT}/profiles/staging; cat > {ROOT}/profiles/staging/{token}.{suffix}",
                    content=contents.encode(), timeout=15)
        self.command("profile-failed" if failed else "profile-load", pad, token)
        for _ in range(40):
            state = json.loads(self.remote(f"cat {RUNTIME}/status.json", timeout=15))
            definition = next((row for row in state.get("pads", []) if row["id"] == pad), {})
            response = definition.get("calibration", {})
            if response.get("attempt") != token:
                time.sleep(.2)
                continue
            if failed and response.get("error") == contents[:500]:
                return
            if not failed:
                if (response.get("state") == "active" and response.get("session") == expected["session"]
                        and response.get("sourceHash") == expected["source_hash"] and not response.get("error")):
                    return
                if response.get("error"):
                    raise CalibrationRejected(response["error"])
            time.sleep(.2)
        raise TimeoutError("The module did not confirm its response profile; check that its software is current")

    def calibrate(self, pad: str, session: str, destination: Path, report: dict):
        from calibrate_pad import ALGORITHM, RecordingIntegrityError, fit_session, native_profile
        previous = report.get("response_calibration", {})
        if previous.get("algorithm") == ALGORITHM and previous.get("state") in ("applied", "unavailable", "legacy"):
            return
        guided = []
        for name in report["verified_files"]:
            if name.endswith(".json") and name != "ready.json":
                document = json.loads((destination / name).read_text())
                if document.get("format") == "open-mimic-pad-sequence-v1":
                    guided.append(document)
        if not guided:
            report["response_calibration"] = {"algorithm": ALGORITHM, "state": "legacy"}
            publish_json(destination / "download.json", report)
            return
        print(f"Calibrating pad response: {pad}", flush=True)
        output = self.output / ".profiles"
        output.mkdir(exist_ok=True)
        attempts = output / pad
        attempts.mkdir(exist_ok=True)
        try:
            profile = fit_session(destination)
            if profile["pad"] != pad or profile["session"] != session:
                raise ValueError("Calibration identity differs from its verified archive")
        except RecordingIntegrityError:
            raise
        except (ValueError, KeyError, TypeError) as error:
            reason = str(error)[:500]
            self.response_result(pad, reason, failed=True)
            result = {"algorithm": ALGORITHM, "state": "unavailable", "reason": reason}
            publish_json(attempts / (session + ".json"), result)
            report["response_calibration"] = result
            publish_json(destination / "download.json", report)
            print(f"Recording saved; previous response retained: {reason}", flush=True)
            return
        try:
            self.response_result(pad, native_profile(profile), failed=False, expected=profile)
        except CalibrationRejected as error:
            result = {"algorithm": ALGORITHM, "state": "unavailable", "reason": str(error)}
            publish_json(attempts / (session + ".json"), result)
            report["response_calibration"] = result
            publish_json(destination / "download.json", report)
            print(f"Recording saved; previous response retained: {error}", flush=True)
            return
        publish_json(attempts / (session + ".json"), profile)
        publish_json(output / (pad + ".json"), profile)
        report["response_calibration"] = {"algorithm": ALGORITHM, "state": "applied", "source_hash": profile["source_hash"]}
        publish_json(destination / "download.json", report)
        print(f"Response applied: {pad}; sensitivity and per-articulation velocity are calibrated", flush=True)

    def model_result(self, pad: str, weights: bytes, description: str, expected_sha256: str):
        token = uuid.uuid4().hex
        staging = f"{ROOT}/models/staging"
        self.remote(f"mkdir -p {staging}; cat > {staging}/{token}.tflite", content=weights, timeout=60)
        self.remote(f"cat > {staging}/{token}.model", content=description.encode(), timeout=15)
        self.command("model-load", pad, token)
        for _ in range(80):
            state = json.loads(self.remote(f"cat {RUNTIME}/status.json", timeout=15))
            definition = next((row for row in state.get("pads", []) if row["id"] == pad), {})
            model = definition.get("model")
            if model is None:
                raise ModelRejected("The module software does not support pad models; update it first")
            if model.get("attempt") != token:
                time.sleep(.25)
                continue
            if model.get("error"):
                raise ModelRejected(model["error"])
            if model.get("state") == "active" and model.get("sha256") == expected_sha256:
                return
            time.sleep(.25)
        raise TimeoutError("The module did not confirm the pad model")

    def adaptation_tool(self, *arguments) -> dict:
        script = Path(__file__).resolve().parent / "pad_model_adaptation.py"
        environment = {**os.environ, "PYTHONPATH": str(script.parent)}
        result = subprocess.run([str(self.ml_python), str(script), *arguments], capture_output=True,
                                text=True, timeout=1800, env=environment)
        lines = [line for line in result.stdout.splitlines() if line.startswith("{")]
        if not lines:
            raise ValueError(f"model adaptation produced no result: {result.stderr[-500:]}")
        document = json.loads(lines[-1])
        if "error" in document:
            raise ModelAdaptationUnavailable(document["error"])
        if result.returncode != 0:
            raise ValueError(f"model adaptation failed: {result.stderr[-500:]}")
        return document

    def adapt_model(self, pad: str, destination: Path, report: dict):
        """Fine-tune the kit's base classifier for one pad and load it on the module.

        Runs after the archive is acknowledged, so a slow or failed adaptation never
        delays releasing the module's copy. The module keeps its shared model as fallback.
        """
        if not self.adapt_models or self.ml_python is None:
            return
        models = self.output / ".models"
        models.mkdir(exist_ok=True)
        manifest = models / "manifest.json"
        self.adaptation_tool("manifest", "--measurements", str(self.output), "--output", str(manifest))
        base = models / "base"
        if not (base / "base.json").exists():
            print("Training the kit's shared base model (one time, a few minutes)...", flush=True)
            self.adaptation_tool("base", "--manifest", str(manifest), "--output-dir", str(base),
                                 "--measurements", str(self.output))
        base_sha256 = json.loads((base / "base.json").read_text())["model_sha256"]
        archived = sum(1 for path in self.output.glob(f"{pad}/*/download.json")
                       if json.loads(path.read_text()).get("device_captures_removed"))
        previous = report.get("model_adaptation", {})
        if (previous.get("state") == "applied" and previous.get("base_sha256") == base_sha256
                and previous.get("archived_sessions") == archived):
            return
        print(f"Adapting the classifier to {pad} from {archived} archived session(s)...", flush=True)
        output = models / "pads" / pad / uuid.uuid4().hex
        try:
            result = self.adaptation_tool("adapt", "--manifest", str(manifest), "--base-dir", str(base),
                                          "--pad", pad, "--output-dir", str(output), "--measurements", str(self.output))
            self.model_result(pad, Path(result["tflite"]).read_bytes(), Path(result["sidecar"]).read_text(),
                              result["model_sha256"])
        except (ModelAdaptationUnavailable, ModelRejected) as error:
            report["model_adaptation"] = {"state": "unavailable", "reason": str(error)[:500], "base_sha256": base_sha256,
                                          "archived_sessions": archived}
            publish_json(destination / "download.json", report)
            print(f"Pad model not applied; the module keeps its shared model: {error}", flush=True)
            return
        report["model_adaptation"] = {"state": "applied", "base_sha256": base_sha256, "model_sha256": result["model_sha256"],
                                      "source": result["source"], "archived_sessions": archived, "windows": result["windows"],
                                      "gate": result["gate"], "artifacts": str(output)}
        publish_json(destination / "download.json", report)
        publish_json(models / "pads" / (pad + ".json"), result)
        print(f"Pad model applied: {pad} ({result['windows']} windows, model {result['model_sha256'][:12]})", flush=True)

    def calibrate_existing(self):
        latest = {}
        for path in self.output.glob("*/*/download.json"):
            pad, session = path.parent.parent.name, path.parent.name
            if not IDENTITY.fullmatch(f"{pad}/{session}"):
                continue
            report = json.loads(path.read_text())
            if not report.get("device_captures_removed"):
                continue
            if pad not in latest or report.get("utc", "") > latest[pad][2].get("utc", ""):
                latest[pad] = (session, path.parent, report)
        for pad, (session, destination, report) in latest.items():
            self.calibrate(pad, session, destination, report)
            self.adapt_model(pad, destination, report)

    def acknowledge(self, pad: str, session: str, destination: Path, report: dict):
        self.calibrate(pad, session, destination, report)
        # Recheck the durable archive immediately before releasing its SD source,
        # including when calibration was unavailable or intentionally skipped.
        verified_files(destination, report["verified_files"])
        self.command("archive-ack", pad, session)
        for _ in range(50):
            acknowledged = self.remote(f"test ! -f {ROOT}/archive-receipts/{pad}/{session}.json || printf ack")
            if acknowledged == "ack":
                report["device_captures_removed"] = True
                publish_json(destination / "download.json", report)
                print(f"Archived and verified {pad}/{session}", flush=True)
                self.adapt_model(pad, destination, report)
                return
            time.sleep(0.2)
        raise ValueError("archive verified locally; module acknowledgment is still pending")

    def archive(self, pad: str, session: str):
        if not IDENTITY.fullmatch(f"{pad}/{session}"):
            raise ValueError("invalid session identity")
        source = f"{ROOT}/dataset/{pad}/{session}"
        destination = self.output / pad / session
        if destination.exists() and self.remote(f"test -d {source} || printf absent") == "absent":
            if destination.is_symlink():
                raise ValueError("archive destination is a symbolic link")
            report = json.loads((destination / "download.json").read_text())
            verified_files(destination, report["verified_files"])
            self.acknowledge(pad, session, destination, report)
            return
        print(f"Checking completed recording: {pad}/{session}", flush=True)
        expected = self.checksums(source)
        if destination.exists():
            if destination.is_symlink():
                raise ValueError("archive destination is a symbolic link")
            report = json.loads((destination / "download.json").read_text())
            recorded = report["verified_files"]
            # Recovery after a failed acknowledgment may find a subset on the SD.
            if any(recorded.get(name) != value for name, value in expected.items()):
                raise ValueError("existing archive conflicts with the module")
            verified_files(destination, recorded)
        else:
            incoming = self.output / ".incoming"
            incoming.mkdir(exist_ok=True)
            staging = incoming / f"{pad}-{session}-{uuid.uuid4().hex}"
            try:
                print(f"Receiving into {destination}", flush=True)
                subprocess.run(["scp", "-O", "-r", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
                                f"{self.host}:{source}", str(staging)], check=True, timeout=180,
                               stdout=None, stderr=None)
                print("Verifying transferred files...", flush=True)
                after = self.checksums(source)
                if after != expected:
                    raise ValueError("source changed during transfer")
                verified_files(staging, expected)
                for path in staging.iterdir():
                    with path.open("rb") as stream:
                        os.fsync(stream.fileno())
                report = {"format": "open-mimic-pad-download-v1", "host": self.host,
                          "source": source, "utc": datetime.now(timezone.utc).isoformat(),
                          "verified_files": expected, "device_captures_removed": False}
                publish_json(staging / "download.json", report)
                destination.parent.mkdir(exist_ok=True)
                flush_directory(self.output)
                os.rename(staging, destination)
                flush_directory(destination.parent)
                flush_directory(incoming)
            except BaseException:
                if staging.exists():
                    shutil.rmtree(staging)
                raise
        # The sealed source cannot receive further writes. Only verified, flushed
        # and atomically published local data can reach this acknowledgment.
        self.acknowledge(pad, session, destination, report)

    def heartbeat(self, ending: threading.Event):
        while not ending.is_set():
            try:
                self.remote(f"touch {RUNTIME}/archive-heartbeat", timeout=8)
            except (OSError, subprocess.SubprocessError):
                pass
            ending.wait(5)

    def error(self, message: str):
        try:
            self.remote(f"cat > {RUNTIME}/archive-error", content=message[:500].encode(), timeout=8)
        except (OSError, subprocess.SubprocessError):
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="root@192.168.31.215")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "measurements")
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--import-existing", action="store_true")
    parser.add_argument("--no-model-adaptation", action="store_true",
                        help="Archive and calibrate responses only; do not fine-tune pad classifiers")
    parser.add_argument("--ml-python", type=Path,
                        default=Path(__file__).resolve().parents[1] / "build/ml-venv/bin/python",
                        help="Python with TensorFlow for pad model adaptation")
    args = parser.parse_args()
    if not HOST.fullmatch(args.host):
        parser.error("invalid SSH host")
    args.output.mkdir(parents=True, exist_ok=True)
    runtime = args.output / ".receiver"
    runtime.mkdir(exist_ok=True)
    lock = (runtime / "receiver.lock").open("w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        parser.exit(1, "Another capture receiver already owns this destination.\n")
    ml_python = args.ml_python if args.ml_python.exists() else None
    if not args.no_model_adaptation and ml_python is None:
        print(f"Pad model adaptation disabled: {args.ml_python} is missing", flush=True)
    receiver = Receiver(args.host, args.output.resolve(), ml_python, not args.no_model_adaptation)
    print(f"Mimic capture receiver: {args.host}", flush=True)
    print(f"Destination: {args.output.resolve()}", flush=True)
    if not args.once:
        print("Keep this terminal open while capturing. Press Ctrl+C to stop.", flush=True)
    ending = threading.Event()
    worker = threading.Thread(target=receiver.heartbeat, args=(ending,), daemon=True)
    worker.start()
    previous_error = None
    connected = False
    try:
        while True:
            try:
                receiver.calibrate_existing()
                for pad, session in receiver.pending_archives():
                    receiver.archive(pad, session)
                for pad, session in receiver.identities(args.import_existing):
                    receiver.archive(pad, session)
                receiver.error("")
                if previous_error is not None or not connected:
                    print("Connected. Waiting for completed pad recordings...", flush=True)
                connected = True
                previous_error = None
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                message = str(error)
                if message != previous_error:
                    print(f"Transfer paused: {message}", flush=True)
                previous_error = message
                connected = False
                receiver.error("Transfer paused. Check the Mac receiver terminal and connection.")
                if args.once:
                    return 1
            if args.once:
                return 0
            ending.wait(2)
    except KeyboardInterrupt:
        return 0
    finally:
        ending.set()
        worker.join(timeout=2)
        if not worker.is_alive():
            try:
                receiver.remote(f"rm -f {RUNTIME}/archive-heartbeat", timeout=2)
            except (OSError, subprocess.SubprocessError):
                pass
        lock.close()
        if not args.once:
            print("Receiver stopped. Pending recordings remain on the module.", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
