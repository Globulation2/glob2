#!/usr/bin/env python3
"""Record two real LAN clients and compare their full per-tick checksum sidecars."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--output", type=Path, required=True, help="A fresh evidence directory")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error("Choose a new output directory; existing evidence is preserved")
    output.mkdir(parents=True)
    binary = args.binary.resolve()
    base = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                GLOB2_TEST_FFMPEG=args.ffmpeg, GLOB2_LAN_ADDRESS="127.0.0.1")
    peers = []
    try:
        with (output / "host.log").open("w") as host_log, (output / "join.log").open("w") as join_log:
            def environment(role):
                profile = output / (role + "-profile")
                profile.mkdir()
                return dict(base, GLOB2_USER_DATA_DIR=str(profile), GLOB2_REPLAY_PATH=str(output / (role + ".replay")))
            host = subprocess.Popen([str(binary), "host", "127.0.0.1", str(output / "host.mp4")],
                                    env=environment("host"), stdout=host_log, stderr=subprocess.STDOUT)
            peers.append(host)
            deadline = time.monotonic() + 20
            endpoint = None
            while time.monotonic() < deadline:
                match = re.search(r"^PAIRING (.+)$", (output / "host.log").read_text(), re.MULTILINE)
                if match:
                    endpoint = match.group(1)
                    break
                if host.poll() is not None:
                    raise RuntimeError("Host exited during setup")
                time.sleep(0.1)
            if endpoint is None:
                raise RuntimeError("Host pairing timed out")
            guest = subprocess.Popen([str(binary), "join", endpoint, str(output / "join.mp4")],
                                     env=environment("join"), stdout=join_log, stderr=subprocess.STDOUT)
            peers.append(guest)
            for peer in peers:
                if peer.wait(timeout=60):
                    raise RuntimeError("A recording peer failed; inspect host.log and join.log")
        host_bytes = (output / "host.replay.checksums").read_bytes()
        guest_bytes = (output / "join.replay.checksums").read_bytes()
        if not host_bytes or host_bytes != guest_bytes:
            raise RuntimeError("Multiplayer per-tick checksum sidecars differ")
        for role in ("host", "join"):
            manifest = json.loads((output / (role + ".mp4.json")).read_text())
            chapters = manifest["chapters"]
            if not manifest["complete"] or not any(c["mode"] == "multiplayer" for c in chapters) or not any(c["phase"] == "results" for c in chapters):
                raise RuntimeError(f"{role} footage is missing match/results chapters")
        print("PASS: both clients recorded multiplayer footage; every checksum record matches")
        return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"Multiplayer recording verification failed: {error}")
        return 1
    finally:
        for peer in peers:
            if peer.poll() is None:
                peer.terminate()
        for peer in peers:
            try:
                peer.wait(timeout=5)
            except subprocess.TimeoutExpired:
                peer.kill()
                peer.wait()


if __name__ == "__main__":
    raise SystemExit(main())
