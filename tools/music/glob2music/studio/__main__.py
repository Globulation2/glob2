# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
import logging
from pathlib import Path
import resource
import random
import time
from types import SimpleNamespace
import zipfile
import numpy as np

from . import score as boundary
from .palettes import Backend, instruments
from ..score import check_score, build_trio, load_composition
from ..spec import DEFAULT_SPEC, MOODS, FILENAMES


def event(**value):
    # Split large measured reports into bounded records without dropping findings.
    check = value.get("check")
    if check and len(json.dumps(value)) > 48000:
        measures = check["measures"]
        for start in range(0, len(measures), 50):
            print(
                json.dumps(
                    {
                        "check": {**check, "measures": measures[start : start + 50]},
                        "append": start > 0,
                    },
                    allow_nan=False,
                ),
                flush=True,
            )
    else:
        print(json.dumps(value, allow_nan=False), flush=True)


def checks(composition, palette):
    findings = check_score(
        composition.SCORE, {m: composition.arrange(m) for m in MOODS}, palette
    )
    return {
        "name": "score",
        "status": (
            "fail"
            if any(f.level == "error" for f in findings)
            else "warn" if any(f.level == "warning" for f in findings) else "pass"
        ),
        "measures": [
            {
                "name": f"score.{i}.{f.kind}",
                "status": {"error": "fail", "warning": "warn", "info": "info"}[f.level],
                "value": None,
                "threshold": "",
                "detail": f.message,
                "unit": "",
            }
            for i, f in enumerate(findings)
        ]
        or [
            {
                "name": "score.findings",
                "status": "pass",
                "value": 0,
                "threshold": "No score defects",
                "detail": "Score checks found no defects.",
                "unit": "findings",
            }
        ],
    }


def write_candidate_preview(directory):
    """Master and check the encoded audition separately from delivery audio."""
    from .. import master, preview
    from ..audio import read_audio, read_trio, write_opus

    audition = master.limit(
        preview.render(read_trio(directory), segment_s=20, crossfade_s=0.37),
        ceiling_dbtp=-2.0,
    )
    for _ in range(3):
        write_opus(audition, directory / "preview.opus")
        decoded, _ = read_audio(directory / "preview.opus")
        peak = master.true_peak_dbtp(decoded)
        if peak <= -1.0:
            break
        audition *= 10 ** ((-1.2 - peak) / 20)
    else:
        raise ValueError("Candidate preview exceeded the encoded peak ceiling")


def render(composition, pipeline, cache, directory, seed, metadata):
    from .. import master
    from ..manifest import encode_within_ceiling
    from ..audio import read_audio
    from ..qa import CHECK_NAMES, run_checks
    from ..qa.analysis import TrioAudio

    directory.mkdir(parents=True, exist_ok=True)
    metadata = {
        **metadata,
        "title": composition.SCORE.title.replace("-", " ").replace("_", " ").title(),
    }
    report = checks(composition, instruments(pipeline))
    event(check=report)
    if report["status"] == "fail":
        return {"passed": False, "checks": [report]}
    ctx = SimpleNamespace(
        set_id=composition.SCORE.title,
        seed=seed,
        work_dir=directory,
        set_dir=directory,
        cache_dir=cache,
        offline=True,
        spec=DEFAULT_SPEC,
        log=logging.getLogger("studio"),
    )
    backend = Backend(pipeline, cache, directory, progress=event)
    trio = build_trio(ctx, composition, backend)
    event(stage="master", detail="Loop-aware mastering and frame-exact Opus encoding")
    trio = master.finish(trio)
    encode_within_ceiling(trio, directory)
    from ..community import tag_opus, comments

    for mood in MOODS:
        tag_opus(
            directory / FILENAMES[mood], comments(metadata, mood, trio.frames, None)
        )
    # All ten checks inspect the exact encoded files that will be delivered.
    event(stage="checks", detail="Checking the encoded three-mood set")
    audio = TrioAudio.load(directory)
    reports = [report]
    for name in CHECK_NAMES:
        report = run_checks(audio, only=[name])
        result = (
            report.results[0].to_dict()
            if report.results
            else {"name": name, "status": "skip", "measures": []}
        )
        for measure in result["measures"]:
            if measure.pop("waiver", ""):
                raise ValueError("Studio deliveries cannot contain waivers")
        reports.append(result)
        event(check=result)
    # A skipped measurement must never silently qualify a paid delivery.
    passed = all(
        r["status"] not in ("fail", "skip", "waived") and r["measures"] for r in reports
    )
    passed = passed and not any(
        m["status"] in ("fail", "skip", "waived")
        for r in reports
        for m in r["measures"]
    )
    tracks = []
    for mood in MOODS:
        path = directory / FILENAMES[mood]
        decoded, rate = read_audio(path)
        if (
            rate != 48000
            or len(decoded) != trio.frames
            or not np.isfinite(decoded).all()
        ):
            raise ValueError("Encoded audio failed the delivery boundary")
        blocks = np.array_split(np.max(np.abs(decoded), axis=1), 512)
        tracks.append(
            {
                "mood": mood,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "bytes": path.stat().st_size,
                "url": "",
                "waveform": [round(float(min(1, np.max(b))), 4) for b in blocks],
            }
        )
    write_candidate_preview(directory)
    warnings = [
        m["detail"] for r in reports for m in r["measures"] if m["status"] == "warn"
    ]
    with zipfile.ZipFile(
        directory / "set.zip", "w", compression=zipfile.ZIP_STORED
    ) as archive:
        for mood in MOODS:
            archive.write(
                directory / FILENAMES[mood], f"{metadata['id']}/{FILENAMES[mood]}"
            )
    result = {
        "passed": passed,
        "checks": reports,
        "metadata": {k: v for k, v in metadata.items() if k not in ("id", "origin")},
        "result": {
            "timelineId": boundary.timeline_id(composition),
            "frames": trio.frames,
            "tracks": tracks,
            "warnings": warnings,
        },
    }
    (directory / "waveforms.json").write_text(
        json.dumps([{"mood": t["mood"], "waveform": t["waveform"]} for t in tracks])
    )
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["export", "check", "render", "probe"])
    parser.add_argument("--job", type=Path, default=Path("/job"))
    parser.add_argument(
        "--pipeline", choices=["acoustic-v1", "synth-v1"], default="acoustic-v1"
    )
    parser.add_argument("--cache", type=Path, default=Path("/assets"))
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    if args.command == "probe":
        import soundfile, scipy, librosa, pyloudnorm, mido, pedalboard  # noqa: F401
        import shutil

        if (
            not shutil.which("ffmpeg")
            or not shutil.which("ffprobe")
            or not (Path(__file__).resolve().parents[3] / "encode_music.py").is_file()
        ):
            raise RuntimeError("The trusted encoder and FFmpeg must be installed")
        import subprocess

        acoustic = Backend("acoustic-v1", args.cache, args.job)
        synth = Backend("synth-v1", args.cache, args.job)
        subprocess.run(
            [str(acoustic.backends[0].binary), "--help"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        pedalboard.load_plugin(str(synth.backends[0].bundle))
        event(ready=True)
        return
    if args.command == "export":
        # Only this command imports generated Python. Its output remains untrusted.
        # Fix ordinary composition helpers before module import and arrangement.
        # Rendering humanisation uses this same request seed in build_trio.
        random.seed(args.seed)
        np.random.seed(args.seed)
        value = boundary.export(load_composition(args.job / "composition.py"))
        text = json.dumps(
            value,
            default=lambda v: sorted(v) if isinstance(v, set) else v,
            allow_nan=False,
        )
        if len(text.encode()) > boundary.MAX_JSON:
            raise ValueError("Score too large")
        (args.job / "score.json").write_text(text)
        return
    composition = boundary.load(
        (args.job / "score.json").read_text(), instruments(args.pipeline)
    )
    if args.command == "check":
        result = checks(composition, instruments(args.pipeline))
        event(check=result)
        (args.job / "result.json").write_text(
            json.dumps({"passed": result["status"] != "fail", "checks": [result]})
        )
    else:
        started = time.monotonic()
        result = render(
            composition,
            args.pipeline,
            args.cache,
            args.job / "output",
            args.seed,
            json.loads((args.job / "metadata.json").read_text()),
        )
        own, child = resource.getrusage(resource.RUSAGE_SELF), resource.getrusage(
            resource.RUSAGE_CHILDREN
        )
        result["resources"] = {
            "elapsedSeconds": time.monotonic() - started,
            "cpuSeconds": own.ru_utime + own.ru_stime + child.ru_utime + child.ru_stime,
            "peakProcessRssKiB": max(own.ru_maxrss, child.ru_maxrss),
        }
        (args.job / "result.json").write_text(json.dumps(result, allow_nan=False))


if __name__ == "__main__":
    main()
