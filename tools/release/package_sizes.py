#!/usr/bin/env python3
"""Inventory staged payloads and compare same-source, same-toolchain packages."""

import argparse
import json
import platform
import sys
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scons"))
from tool_archives import digest


def category(relative):
    path = Path(relative)
    if path.suffix.lower() in (".exe", ".dll", ".dylib") or re.search(
        r"\.so(?:\.\d+)*$", path.name
    ):
        return "executables" if path.suffix.lower() == ".exe" else "runtime-libraries"
    if path.name in ("glob2", "glob2.exe"):
        return "executables"
    parts = path.parts
    for name, group in (
        ("highres", "hd-artwork"),
        ("fonts", "fonts"),
        ("zik", "audio"),
        ("gfx", "graphics"),
        ("maps", "maps"),
        ("campaigns", "campaigns"),
        ("scripts", "scripts"),
    ):
        if name in parts:
            return group
    return "other"


def inventory(
    staged,
    archives=(),
    profile="current",
    label="candidate",
    scope="standalone-artifact",
    compiler="c++",
):
    staged = Path(staged).resolve()
    if not staged.is_dir():
        raise ValueError("Staged root is not a directory")
    files = []
    groups = {}
    for path in sorted(staged.rglob("*")):
        relative = path.relative_to(staged).as_posix()
        if path.is_symlink():
            files.append(
                dict(
                    path=relative,
                    kind="symlink",
                    target=path.readlink().as_posix(),
                    bytes=0,
                )
            )
        elif path.is_file():
            size = path.stat().st_size
            group = category(relative)
            groups[group] = groups.get(group, 0) + size
            files.append(
                dict(
                    path=relative,
                    kind="file",
                    bytes=size,
                    category=group,
                    sha256=digest(path),
                )
            )
    packed = []
    for archive in archives:
        path = Path(archive)
        packed.append(
            dict(name=path.name, bytes=path.stat().st_size, sha256=digest(path))
        )
    return dict(
        schema=1,
        label=label,
        scope=scope,
        profile=profile,
        revision=subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
        ).strip(),
        platform=platform.system(),
        architecture=platform.machine(),
        compiler=(
            subprocess.check_output([compiler, "--version"], text=True).splitlines()[0]
            if compiler != "unrecorded"
            else None
        ),
        payload_bytes=sum(groups.values()),
        categories=groups,
        files=files,
        archives=packed,
    )


def compare(baseline, candidate):
    if not baseline.get("compiler") or not candidate.get("compiler"):
        raise ValueError("Comparison requires recorded compiler identities")
    for field in (
        "schema",
        "revision",
        "scope",
        "platform",
        "architecture",
        "compiler",
    ):
        if baseline[field] != candidate[field]:
            raise ValueError("Comparison requires matching " + field)
    before, after = baseline["payload_bytes"], candidate["payload_bytes"]
    categories = {
        name: baseline["categories"].get(name, 0) - candidate["categories"].get(name, 0)
        for name in sorted(
            baseline["categories"].keys() | candidate["categories"].keys()
        )
    }
    return dict(
        baseline=baseline["label"],
        candidate=candidate["label"],
        category_savings=categories,
        baseline_bytes=before,
        candidate_bytes=after,
        saved_bytes=before - after,
        saved_percent=100 * (before - after) / before if before else 0,
        baseline_archives=baseline["archives"],
        candidate_archives=candidate["archives"],
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    report = commands.add_parser("report")
    report.add_argument("--staged-root", type=Path, required=True)
    report.add_argument("--archive", type=Path, action="append", default=[])
    report.add_argument("--profile", default="current")
    report.add_argument("--label", default="candidate")
    report.add_argument(
        "--scope",
        choices=("standalone-artifact", "asset-only"),
        default="standalone-artifact",
    )
    report.add_argument(
        "--compiler",
        default="c++",
        help="Compiler command, or unrecorded for externally built packages",
    )
    comparison = commands.add_parser("compare")
    comparison.add_argument("baseline", type=Path)
    comparison.add_argument("candidate", type=Path)
    for command in (report, comparison):
        command.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = (
        inventory(
            args.staged_root,
            args.archive,
            args.profile,
            args.label,
            args.scope,
            args.compiler,
        )
        if args.command == "report"
        else compare(
            json.loads(args.baseline.read_text()),
            json.loads(args.candidate.read_text()),
        )
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
