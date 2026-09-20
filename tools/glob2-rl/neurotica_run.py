# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared generated-game runner and immutable policy-server lifecycle."""

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid

HERE = Path(__file__).resolve().parent
GAME_END = re.compile(r"GLOB2_GAME_END ticks=(\d+) winner_team=(-?\d+)")


def end_reason(winner, ticks, max_ticks):
    if winner not in (-1, 0, 1):
        raise ValueError("unexpected winner")
    return (
        ("cap" if ticks >= max_ticks else "draw")
        if winner == -1
        else ("win" if winner == 0 else "loss")
    )


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1024 * 1024), b""):
            h.update(b)
    return h.hexdigest()


def clean_env():
    return {
        k: v
        for k, v in os.environ.items()
        if not k.startswith(("GLOB2_NEUROTICA_", "GLOB2_TEST_", "GLOB2_REPLAY_"))
    }


def write_json(path, data):
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, indent=2))
    os.replace(tmp, p)


class PolicyServer:
    def __init__(self, checkpoint, out, device="cpu", sample=False, seed=0):
        self.out = Path(out).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.rollouts = self.out / "rollouts"
        self.rollouts.mkdir(exist_ok=True)
        self.socket = f"/tmp/neurotica-{uuid.uuid4().hex[:16]}.sock"
        self.log = open(self.out / "server.log", "w")
        cmd = [
            sys.executable,
            str(HERE / "neurotica_serve.py"),
            "--checkpoint",
            str(Path(checkpoint).resolve()),
            "--socket",
            self.socket,
            "--device",
            device,
            "--seed",
            str(seed),
            "--record-dir",
            str(self.rollouts),
        ]
        if sample:
            cmd.append("--sample")
        self.proc = subprocess.Popen(
            cmd, stdout=self.log, stderr=subprocess.STDOUT, env=clean_env()
        )
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"policy server exited; see {self.out}/server.log")
            if os.path.exists(self.socket):
                return
            time.sleep(0.1)
        self.close()
        raise TimeoutError("server startup")

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=140)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.log.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def play_game(
    root,
    binary,
    g,
    out,
    max_ticks=40000,
    timeout=1800,
    server=None,
    teacher=None,
    record=False,
    roundtrip=False,
    replay=False,
):
    root = Path(root).resolve()
    binary = Path(binary).resolve()
    out = Path(out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    gid = int(g["id"])
    name = f"neurotica_{uuid.uuid4().hex[:12]}"
    map_path = root / "maps" / f"{name}.map"
    result = dict(
        g, status="invalid", max_ticks=max_ticks, timeout=timeout, roundtrip=roundtrip
    )
    env = clean_env()
    env.update(
        GLOB2_TEST_SEED=str(g["game_seed"]),
        GLOB2_TEST_MAX_TICKS=str(max_ticks),
        GLOB2_NEUROTICA_GAME_ID=str(gid),
    )
    profile = out / f"g{gid}.profile"
    profile.mkdir(exist_ok=True)
    env["GLOB2_USER_DIR"] = str(profile)
    env["GLOB2_REPLAY_PATH"] = str(out / f"g{gid}.replay")
    if server:
        env["GLOB2_NEUROTICA_POLICY_SOCKET"] = server.socket
    if record:
        env["GLOB2_NEUROTICA_ACTION_CORPUS"] = str(out / f"g{gid}")
    if roundtrip:
        env["GLOB2_NEUROTICA_ACTION_ROUNDTRIP"] = "1"
    if replay:
        env["GLOB2_REPLAY_PATH"] = str(out / f"g{gid}.replay")
    try:
        gen = subprocess.run(
            [
                str(binary),
                "--generate-map",
                g["generator"],
                "--output",
                str(out / f"g{gid}.map"),
                "--width",
                "128",
                "--height",
                "128",
                "--teams",
                "2",
                "--seed",
                str(g["map_seed"]),
            ],
            cwd=root,
            env=env,
            capture_output=True,
            text=True,
            timeout=300,
        )
        (out / f"g{gid}.mapgen.log").write_text(gen.stdout + gen.stderr)
        if gen.returncode or not (out / f"g{gid}.map").exists():
            raise RuntimeError("map generation failed")
        result["map_sha256"] = digest(out / f"g{gid}.map")
        # Preserve map itself as a reproducible fixture, not just its seed.
        import shutil

        shutil.copyfile(out / f"g{gid}.map", map_path)
        proc = subprocess.run(
            [
                str(binary),
                "-test-games-nox",
                "1",
                "--map",
                name,
                "--matchup",
                f"{teacher or 'neurotica'},{g['opponent']}",
            ],
            cwd=root,
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        output = proc.stdout + proc.stderr
        (out / f"g{gid}.log").write_text(output)
        matches = GAME_END.findall(output)
        if proc.returncode or len(matches) != 1 or "NEUROTICA_POLICY_FAILURE" in output:
            raise RuntimeError("game failed or ambiguous result")
        ticks, winner = map(int, matches[0])
        result.update(ticks=ticks, winner=winner)
        result["end_reason"] = end_reason(winner, ticks, max_ticks)
        if server:
            if server.proc.poll() is not None:
                raise RuntimeError("policy server died during match")
            p = server.rollouts / f"g{gid}.t0.json"
            deadline = time.monotonic() + 15
            while not p.exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            if not p.exists():
                raise RuntimeError("missing policy trajectory")
            meta = json.loads(p.read_text())
            if meta["status"] != "complete" or meta["steps"] < 1:
                raise RuntimeError("invalid policy trajectory")
            import numpy as np

            with np.load(p.with_suffix(".npz")) as a:
                last = int(a["ticks"][-1])
                last_action = a["actions"][
                    a["action_offsets"][-2] : a["action_offsets"][-1]
                ].tobytes()
            from neurotica_actions import unpack_action

            if ticks - last > unpack_action(last_action)["delay"] + 2:
                raise RuntimeError("policy stopped acting before match ended")
            result.update(
                policy_id=meta["policy_id"],
                seed=meta["seed"],
                sample=meta["sample"],
                steps=meta["steps"],
            )
            meta.update(
                outcome=int(winner == 0),
                end_tick=ticks,
                end_reason=result["end_reason"],
                opponent=g["opponent"],
            )
            write_json(p, meta)
        result["status"] = "complete"
    except (RuntimeError, TimeoutError, subprocess.TimeoutExpired, ValueError) as exc:
        result["error"] = str(exc)
        if server:
            p = server.rollouts / f"g{gid}.t0.json"
            if p.exists():
                meta = json.loads(p.read_text())
                meta.update(status="invalid", error=str(exc))
                write_json(p, meta)
    finally:
        map_path.unlink(missing_ok=True)
    write_json(out / f"g{gid}.json", result)
    return result
