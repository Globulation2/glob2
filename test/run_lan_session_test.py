#!/usr/bin/env python3
"""Run real LAN host and guest processes on loopback.

By default: two join/ready/leave cycles with exact map downloads. With --play SECONDS:
the host starts a real game through the room screen, both play for SECONDS, the host
quits, and the guest's game must end with "host left"; the two processes' per-tick
checksum sidecars must agree on every tick both executed.
"""
import argparse
import os
from pathlib import Path
import subprocess
import time
import re
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=Path("artifacts/lan-session-test"))
    parser.add_argument("--play", type=int, metavar="SECONDS", help="play a game instead of the lobby cycles")
    parser.add_argument("--compare", nargs=2, type=Path, metavar="CHECKSUMS",
                        help="only compare two .checksums sidecars from a LAN game (binary is ignored)")
    args = parser.parse_args()
    if args.compare:
        return compare(*args.compare)
    binary = args.binary.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.play:
        return play(binary, output, args.play)
    env = os.environ.copy()
    env.setdefault("SDL_VIDEODRIVER", "dummy")
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    host_log = output / "host.log"
    join_log = output / "join.log"
    with tempfile.TemporaryDirectory(prefix="glob2-lan-test-") as profiles, host_log.open("w") as host_out, join_log.open("w") as join_out:
        host_env = dict(env, GLOB2_LAN_ADDRESS="127.0.0.1", GLOB2_USER_DIR=str(Path(profiles) / "host"))
        join_env = dict(env, GLOB2_USER_DIR=str(Path(profiles) / "join"))
        host = subprocess.Popen(
            [str(binary), "host", "127.0.0.1", "2", str(output / "host")],
            stdout=host_out, stderr=subprocess.STDOUT, env=host_env,
        )
        try:
            deadline = time.monotonic() + 20
            while "HOST roster=1" not in host_log.read_text():
                if host.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("Host did not publish a lobby")
                time.sleep(0.1)
            pairing = re.search(r"^PAIRING (.+)$", host_log.read_text(), re.MULTILINE).group(1)
            joined = subprocess.run(
                [str(binary), "join", pairing, "2", str(output / "join")],
                stdout=join_out, stderr=subprocess.STDOUT, env=join_env, timeout=100,
            )
            if joined.returncode != 0:
                raise RuntimeError(f"Join/transfer/leave test exited {joined.returncode}")
            if host.wait(timeout=15) != 0:
                raise RuntimeError("Host roster/readiness verification failed")
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            print(f"LAN session regression FAIL: {error}")
            print(host_log.read_text())
            print(join_log.read_text())
            return 1
        finally:
            if host.poll() is None:
                host.terminate()
                try:
                    host.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    host.kill()
                    host.wait()
    print(host_log.read_text())
    print(join_log.read_text())
    print("LAN session regression PASS: two ready/join/leave cycles and exact map downloads")
    return 0


def sidecar_body(path):
    data = path.read_bytes()
    if len(data) < 20:
        raise RuntimeError(f"{path} is not a checksum sidecar")
    return data[:12], data[20:]


def play(binary, output, seconds):
    env = os.environ.copy()
    env.setdefault("SDL_VIDEODRIVER", "dummy")
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    host_log, join_log = output / "host-play.log", output / "join-play.log"
    replays = {name: output / f"{name}.replay" for name in ("host", "join")}
    for path in replays.values():
        for stale in (path, path.with_name(path.name + ".checksums")):
            if stale.exists():
                stale.unlink()
    with tempfile.TemporaryDirectory(prefix="glob2-lan-play-") as profiles, host_log.open("w") as host_out, join_log.open("w") as join_out:
        common = dict(env, GLOB2_CHECKSUM_SIDECAR="1")
        host_env = dict(common, GLOB2_LAN_ADDRESS="127.0.0.1", GLOB2_USER_DIR=str(Path(profiles) / "host"),
                        GLOB2_REPLAY_PATH=str(replays["host"]))
        join_env = dict(common, GLOB2_USER_DIR=str(Path(profiles) / "join"), GLOB2_REPLAY_PATH=str(replays["join"]))
        host = subprocess.Popen([str(binary), "host-play", "127.0.0.1", str(seconds), str(output / "host-play")],
                                stdout=host_out, stderr=subprocess.STDOUT, env=host_env)
        join = None
        try:
            deadline = time.monotonic() + 20
            while "PAIRING" not in host_log.read_text():
                if host.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("Host did not publish a room")
                time.sleep(0.1)
            pairing = re.search(r"^PAIRING (.+)$", host_log.read_text(), re.MULTILINE).group(1)
            join = subprocess.Popen([str(binary), "join-play", pairing, "1", str(output / "join-play")],
                                    stdout=join_out, stderr=subprocess.STDOUT, env=join_env)
            if host.wait(timeout=seconds + 150) != 0:
                raise RuntimeError("Host play failed")
            deadline = time.monotonic() + 30
            while "relay refused us: host-left" not in join_log.read_text():
                if join.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("The guest's game did not end when the host left")
                time.sleep(0.2)
            time.sleep(2)  # the guest closes its replay and sidecar as the session ends
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            print(f"LAN play FAIL: {error}")
            print(host_log.read_text())
            print(join_log.read_text())
            return 1
        finally:
            for process in (host, join):
                if process and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
    print(host_log.read_text())
    print(join_log.read_text())
    if compare(replays["host"].with_name("host.replay.checksums"), replays["join"].with_name("join.replay.checksums")):
        print("LAN play FAIL")
        return 1
    print("LAN play PASS")
    return 0


def compare(first, second):
    """Two players' sidecars agree on every tick both executed: records have the same
    layout in both files, so the shorter run's records are a prefix of the longer's."""
    header_a, body_a = sidecar_body(first)
    header_b, body_b = sidecar_body(second)
    common = min(len(body_a), len(body_b))
    if header_a != header_b or common < 1000 or body_a[:common] != body_b[:common]:
        print(f"checksum sidecars DIFFER ({first}: {len(body_a)} bytes, {second}: {len(body_b)} bytes)")
        return 1
    print(f"checksum sidecars AGREE on {common} bytes of per-tick records "
          f"({first}: {len(body_a)}, {second}: {len(body_b)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
