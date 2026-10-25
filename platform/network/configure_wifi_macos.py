#!/usr/bin/env python3
"""Write a WPA-PSK config to the verified Open Mimic SD card.

The password is entered locally without echo and is never passed as a shell
argument or written in plaintext. The derived PSK remains on the SD card.
"""

import getpass
import hashlib
import os
import plistlib
import subprocess
import sys
import tempfile
from pathlib import Path


BOOT = Path("/Volumes/MIMICBOOT")
PUBLIC_KEYS = (Path.home() / ".ssh/id_ed25519.pub", Path.home() / ".ssh/id_rsa.pub")


def disk_info(target: str) -> dict:
    return plistlib.loads(subprocess.check_output(["diskutil", "info", "-plist", target]))


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} SSID", file=sys.stderr)
        return 2

    ssid = sys.argv[1]
    ssid_bytes = ssid.encode("utf-8")
    if not 1 <= len(ssid_bytes) <= 32:
        print("SSID must contain 1–32 UTF-8 bytes.", file=sys.stderr)
        return 2

    volume = disk_info(str(BOOT))
    disk = volume.get("ParentWholeDisk")
    if not isinstance(disk, str) or not disk.startswith("disk"):
        raise RuntimeError("Could not identify the parent SD disk")
    whole = disk_info(f"/dev/{disk}")
    if not (
        volume.get("VolumeName") == "MIMICBOOT"
        and volume.get("MountPoint") == str(BOOT)
        and whole.get("BusProtocol") == "Secure Digital"
        and whole.get("RemovableMedia") is True
        and whole.get("OSInternalMedia") is False
    ):
        raise RuntimeError("This is not the expected Open Mimic SD card")

    if not all(
        (BOOT / name).is_file()
        for name in ("ZT9101.KO", "ZT9101.FW", "WIFI.CFG", "PEARLKRN.BIN", "PEARL571.DTB")
    ):
        raise RuntimeError("The Open Mimic driver or boot files are missing")
    key_path = next((path for path in PUBLIC_KEYS if path.is_file()), None)
    if key_path is None:
        raise RuntimeError("No SSH public key found in ~/.ssh")
    public_key = key_path.read_text(encoding="ascii").strip()
    if not public_key.startswith(("ssh-rsa ", "ssh-ed25519 ", "ecdsa-sha2-")):
        raise RuntimeError(f"Invalid SSH public key: {key_path}")

    password = getpass.getpass(f"Wi-Fi password for {ssid}: ")
    password_bytes = password.encode("utf-8")
    if not 8 <= len(password_bytes) <= 63:
        raise ValueError("The WPA passphrase must contain 8–63 UTF-8 bytes")

    psk = hashlib.pbkdf2_hmac("sha1", password_bytes, ssid_bytes, 4096, 32)
    config = (
        "ctrl_interface=/tmp/wpa_supplicant\n"
        "update_config=0\n"
        "network={\n"
        f"    ssid={ssid_bytes.hex()}\n"
        f"    psk={psk.hex()}\n"
        "    key_mgmt=WPA-PSK\n"
        "    scan_ssid=1\n"
        "}\n"
    )
    def atomic_write(name: str, content: str) -> None:
        temp_path = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="w", encoding="ascii", dir=BOOT, prefix=".open-mimic-", delete=False
            ) as temp:
                temp.write(content)
                temp.flush()
                os.fsync(temp.fileno())
                temp_path = Path(temp.name)
            os.replace(temp_path, BOOT / name)
        finally:
            if temp_path is not None and temp_path.exists():
                temp_path.unlink()

    atomic_write("WPA.CONF", config)
    atomic_write("AUTHKEYS", public_key + "\n")
    os.sync()

    subprocess.run(["diskutil", "eject", f"/dev/{disk}"], check=True)
    print("Wi-Fi settings and SSH public key written; SD card ejected. Password was not displayed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(1)
