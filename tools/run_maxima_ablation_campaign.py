#!/usr/bin/env python3
"""Resume a locked, remote-only Maxima campaign in Wave 1 -> 2 -> 3 order.

The campaign never edits strategy defaults. Fixed 70-block from-start screens
supplement the existing opportunity banks. Conditional pilots choose sample
size using variance, with 80/160/320-block targets and a 512-source-block
harvest cap. Missing opportunities remain explicitly unresolved.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
import csv
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time
import traceback

import maxima_efficient_design as efficient
import maxima_campaign_bank as bank
import run_maxima_switch_ablation as ablation

ROOT = Path(__file__).resolve().parents[1]
FORMATS = ("duel", "ffa3", "ffa4", "ffa5", "2v2")
FORMAT_CODES = {"duel": 0, "2v2": 1, "ffa3": 2, "ffa4": 3, "ffa5": 4}
FFA_MAPS = {
    "ffa3": ("A_big_pond", "Easy_Three", "Triangle", "G2", "Island_of_the_Renfur",
             "Garden_3", "Centerfolds_2"),
    "ffa4": ("FourSquares1", "G2", "Holiday_Island_2", "Migration", "Archipelago",
             "Garden_3", "Isles"),
    "ffa5": ("Archipelago", "Island_of_the_Renfur", "Sand_River", "Centerfolds_2",
             "Wild_River", "Playground", "Oazis"),
}


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def atomic(path: Path, value: dict) -> None:
    pending = path.with_suffix(path.suffix + ".tmp")
    pending.write_text(json.dumps(value, indent=2) + "\n")
    pending.replace(path)


class Campaign:
    def __init__(self, output: Path, adopt_running: bool = False):
        self.output = output.resolve()
        self.lock = json.loads((self.output / "TOURNAMENT_LOCK.json").read_text())
        self.remote = self.lock["remote_root"]
        self.state_path = self.output / "STATUS.json"
        self.state = json.loads(self.state_path.read_text()) if self.state_path.exists() else {
            "completed_stages": [], "results": [], "started_utc": now(),
        }
        self.adoption = ({"stage": self.state.get("stage"), "command": self.state.get("command"),
                          "pid": self.state.get("child_pid")} if adopt_running else None)
        self.state.update(pid=os.getpid(), status="running", error=None)
        self.save()

    def save(self, **values):
        self.state.update(values, updated_utc=now())
        atomic(self.state_path, self.state)

    def verify(self):
        for relative, digest in self.lock["controller_files"].items():
            if hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() != digest:
                raise RuntimeError(f"frozen controller file changed: {relative}")
        def verify_host(worker):
            host, _ = worker
            result = subprocess.run(
                ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", host,
                 f"cd {shlex.quote(self.remote)} && sha256sum --quiet -c RUNTIME_SHA256SUMS"],
                capture_output=True, text=True, timeout=90)
            if result.returncode:
                raise RuntimeError(f"runtime lock verification failed on {host}: "
                                   + result.stdout + result.stderr)
        with ThreadPoolExecutor(max_workers=5) as executor:
            list(executor.map(verify_host, self.lock["workers"]))

    def command(self, stage: str, command: list[str], allowed=(0,)):
        if stage in self.state["completed_stages"]:
            return
        if (self.output / "RETIRED.json").exists():
            raise RuntimeError("Campaign retired; further dispatch forbidden")
        self.verify()
        log_path = self.output / (stage + ".log")
        self.save(stage=stage, command=command, log=str(log_path))
        if self.adoption and self.adoption["stage"] == stage:
            if self.adoption["command"] != command:
                raise RuntimeError("cannot adopt a stage with different command/settings")
            pid = self.adoption["pid"]
            if not pid:
                raise RuntimeError("adopted stage has no recorded worker process")
            print(f"{now()} ADOPT {stage}: existing PID {pid}; no runs restarted", flush=True)
            while True:
                process = subprocess.run(["ps", "-p", str(pid), "-o", "stat=,command="],
                                         text=True, capture_output=True, check=False)
                description = process.stdout.strip()
                if not description or description.startswith("Z"):
                    break
                if command[2] not in description or command[3] not in description:
                    raise RuntimeError("adopted PID no longer identifies the expected stage")
                time.sleep(5)
            self.adoption = None
            self.save(child_pid=None)
            print(f"{now()} FINISH adopted {stage}; validating persisted results", flush=True)
            return
        print(f"{now()} START {stage}", flush=True)
        with log_path.open("a") as log:
            log.write("\nCOMMAND " + shlex.join(command) + "\n"); log.flush()
            child = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            self.save(child_pid=child.pid)
            code = child.wait()
        self.save(child_pid=None)
        if code not in allowed:
            raise RuntimeError(f"{stage} exited {code}; see {log_path}")
        print(f"{now()} FINISH {stage} exit={code}", flush=True)

    def mark(self, stage):
        if stage not in self.state["completed_stages"]:
            self.state["completed_stages"].append(stage)
        self.save()

    def harvest(self, name: str, fmt: str, rounds: int, seed: int,
                opponent: int, horizon=60000, seats=True) -> Path:
        newly_harvested = name not in self.state["completed_stages"]
        directory = self.output / name
        command = [sys.executable, "-u", str(ROOT / "tools/harvest_maxima_checkpoints.py"),
                   "--output-dir", str(directory), "--remote-root", self.remote,
                   "--format", fmt, "--rounds", str(rounds), "--seed", str(seed),
                   "--opponent-ai", str(opponent), "--max-steps", str(horizon),
                   "--interval", "1000", "--quota", "512", "--timeout", "1800"]
        if seats and fmt == "duel": command.append("--all-seats")
        if seats and fmt in FFA_MAPS: command.extend(["--seat-rotations", "2"])
        for map_name in FFA_MAPS.get(fmt, ()):
            command.extend(["--map", f"maps/{map_name}.map"])
        self.command(name, command, (0, 1))
        source_file = directory / "source-runs.json"
        if not source_file.exists():
            raise RuntimeError(f"{name}: harvest did not produce source results")
        sources = json.loads(source_file.read_text())
        successful = sum(not row["error"] for row in sources)
        if successful < math.ceil(len(sources) * .90):
            raise RuntimeError(f"{name}: only {successful}/{len(sources)} source games succeeded")
        self.mark(name)
        compressor = self.output / "controller-revisions/apfs-storage-v1/compress_completed_sources.py"
        if (newly_harvested and sys.platform == "darwin" and compressor.is_file()
                and shutil.disk_usage(self.output).free < 40 * 1024**3):
            # Each bank can consume several GiB before the hourly monitor runs.
            # Completed banks are immutable; this verifies identical bytes and
            # preserves paths while recovering space before the next harvest.
            with (self.output / "storage-maintenance.log").open("a") as log:
                subprocess.run([sys.executable, str(compressor)], check=True,
                               stdout=log, stderr=subprocess.STDOUT, timeout=900)
        return directory

    def discovery_banks(self, fmt: str) -> list[Path]:
        offset = FORMAT_CODES[fmt] * 20000000
        return [self.harvest(f"discovery-{fmt}-opponent{opponent}", fmt, 5,
                             1510000000 + offset + index * 10000000, opponent)
                for index, opponent in enumerate((5, 6))]

    def expansion_banks(self, fmt: str, index: int) -> list[Path]:
        # At most six 70-block late-game batches in addition to discovery:
        # 490 map/seed blocks, below the predeclared 512-block harvest cap.
        offset = FORMAT_CODES[fmt] * 100000000
        return [self.harvest(f"events-{fmt}-batch{index}-opponent{opponent}", fmt, 5,
                             1560000000 + offset + index * 1000000 + side * 500000,
                             opponent, horizon=120000, seats=False)
                for side, opponent in enumerate((5, 6))]

    def strata(self, directory: Path, rows: list[dict]):
        metadata = {r["checkpoint_id"]: r for r in rows}
        grouped = defaultdict(lambda: defaultdict(list))
        with (directory / "paired.csv").open() as handle:
            for paired in csv.DictReader(handle):
                meta = metadata[paired["checkpoint_id"]]
                for field in ("map", "topology", "opponent_ai", "focal_player"):
                    grouped[(paired["switch"], field, str(meta[field]))][paired["source_block"]].append(
                        float(paired["difference"]))
        strata = []
        for (switch, dimension, value), blocks in sorted(grouped.items()):
            values = [sum(v)/len(v) for v in blocks.values()]
            center, low, high = ablation.interval(values)
            strata.append({"switch": switch, "dimension": dimension, "value": value,
                           "independent_blocks": len(values), "mean": center,
                           "ci_low": low if math.isfinite(low) else None,
                           "ci_high": high if math.isfinite(high) else None})
        atomic(directory / "strata.json", {"interpretation": "descriptive, not multiplicity-adjusted",
                                            "strata": strata})

    def run_rows(self, name: str, rows: list[dict], switches: list[str],
                 confirmation=False, result_name: str | None = None) -> dict:
        directory = self.output / (result_name or name)
        manifest = self.output / (name + "-manifest.json")
        accepted = (self.lock.get("resume_persisted_stages", {}).get(name)
                    if name not in self.state["completed_stages"] else None)
        if accepted:
            for filename, digest in accepted.items():
                if hashlib.sha256((self.output/filename).read_bytes()).hexdigest() != digest:
                    raise RuntimeError("persisted stage changed before authorized resumption: " + filename)
            if ablation.read_manifest(manifest) != rows:
                raise RuntimeError("persisted stage settings differ from requested rows")
        # Completed stage manifests are immutable, including the broad legacy screens.
        if name not in self.state["completed_stages"] and not accepted:
            bank.write(manifest, rows, switches)
        if not accepted and name not in self.state["completed_stages"] and not (self.adoption and self.adoption["stage"] == name):
            reused = efficient.seed_cache(self.output, directory, rows, self.state["results"])
            if reused: print(f"{now()} CACHE {name}: copied {reused} qualified branch logs", flush=True)
        if not rows:
            directory.mkdir(parents=True, exist_ok=True)
            summary = {"summaries": [], "qualified_checkpoints": 0,
                       "interpretation": "no qualifying opportunities"}
            atomic(directory / "summary.json", summary)
            self.state["results"] = [r for r in self.state["results"] if r["stage"] != name] + [{
                "stage": name, "summary": str(directory / "summary.json"),
                "manifest": str(manifest), "missing_switches": switches}]
            self.mark(name)
            self.report()
            return summary
        command = [sys.executable, "-u", str(ROOT / "tools/run_maxima_switch_ablation.py"),
                   str(manifest), "--output-dir", str(directory), "--remote-root", self.remote,
                   "--timeout", "2400", "--minimum-blocks", "32"]
        if confirmation: command.append("--confirmation")
        if not accepted:
            self.command(name, command, (0, 1))
        else:
            print(f"{now()} REUSE completed persisted results for {name}", flush=True)
        summary_path = directory / "summary.json"
        if not summary_path.exists():
            raise RuntimeError(f"{name}: no summary was produced")
        summary = json.loads(summary_path.read_text())
        ceiling = self.lock.get("checkpoint_failure_ceiling", 0.10)
        if not 0 <= ceiling < 1:
            raise RuntimeError("invalid checkpoint failure ceiling")
        if summary["qualified_checkpoints"] < math.ceil(len(rows)*(1-ceiling)-1e-12):
            raise RuntimeError(f"{name}: more than {ceiling:.0%} of checkpoints failed qualification")
        summary["qualification_coverage"] = {
            "attempted": len(rows), "qualified": summary["qualified_checkpoints"],
            "failure_ceiling": ceiling,
            "excluded_fraction": 1-summary["qualified_checkpoints"]/len(rows),
            "interpretation": "Failed cases excluded. Inference applies to qualifying states; exclusion may be systematic. Ceiling raised by user after observing FFA4 failures."}
        if not accepted:
            atomic(summary_path, summary)
        self.strata(directory, rows)
        entry = {"stage": name, "summary": str(summary_path), "manifest": str(manifest),
                 "missing_switches": sorted(set(switches)-{r["switch"] for r in summary["summaries"]})}
        self.state["results"] = [r for r in self.state["results"] if r["stage"] != name] + [entry]
        self.mark(name)
        self.report()
        return summary

    def legacy_conditional(self, wave: int, fmt: str, directories: list[Path], switches: list[str]) -> dict:
        prefix = f"wave{wave}-{fmt}-contexts"
        pilot = bank.build(directories, switches, "context", 20000, quota=40)
        summary = self.run_rows(prefix+"-pilot", pilot, switches)
        targets_path = self.output / (prefix + "-targets.json")
        if targets_path.exists():
            targets = json.loads(targets_path.read_text())
        else:
            # Choose size from variance, never from an interim significance look.
            targets = {s["switch"]: next((n for n in (80, 160, 320)
                        if n >= s["required_blocks_90pct"]), 320)
                       for s in summary["summaries"] if s["independent_blocks"] >= 32}
            atomic(targets_path, targets)
        selected = []
        if targets:
            # Other rare switches still get a 40-source pilot, when naturally
            # available in the same late-game extension banks.
            desired = {key: targets.get(key, 40) for key in switches}
            for index in range(7):
                selected = []
                missing = []
                for key, count in desired.items():
                    values = bank.build(directories, [key], "context", 20000, quota=count)
                    selected.extend(values)
                    if len({r["source_block"] for r in values}) < count:
                        missing.append(key)
                if not missing or index == 6:
                    break
                # Do not spend hundreds of ordinary games chasing switches with
                # no supported trigger telemetry. They have from-start screens.
                observable = {r["switch"] for r in pilot}
                if not (set(missing) & observable): break
                directories.extend(self.expansion_banks(fmt, index))
            summary = self.run_rows(prefix+"-discovery", selected, switches)
        return summary

    def conditional(self, wave: int, fmt: str, directories: list[Path], switches: list[str]) -> dict:
        legacy = f"wave{wave}-{fmt}-contexts"
        completed = legacy+"-discovery"
        if completed in self.state["completed_stages"]:
            return json.loads((self.output/completed/"summary.json").read_text())
        if self.adoption and self.adoption["stage"] == completed:
            rows = ablation.read_manifest(self.output/(completed+"-manifest.json"))
            return self.run_rows(completed, rows, switches)
        # Finish already-started experiments exactly as declared; never retrofit stopping.
        if legacy+"-pilot" in self.state["completed_stages"] or (
                self.adoption and str(self.adoption["stage"]).startswith(legacy)):
            return self.legacy_conditional(wave, fmt, directories, switches)
        keys = efficient.route(switches, "CP")
        prefix = f"wave{wave}-{fmt}-contexts-v2"
        for index in range(6):
            for opponent in (5, 6):
                name = f"events-{fmt}-batch{index}-opponent{opponent}"
                if name in self.state["completed_stages"] and self.output/name not in directories:
                    directories.append(self.output/name)
        selected = []; active = list(keys); stopped = {}; targets = {}; final = {"summaries": []}
        for look in efficient.LOOKS:
            if not active: break
            # Freeze one cumulative manifest per look before starting any branches.
            name = prefix+f"-n{look}"
            manifest = self.output/(name+"-manifest.json")
            if manifest.exists():
                selected = ablation.read_manifest(manifest)
            else:
                while True:
                    candidates = bank.build(directories, active, "context", 20000)
                    for row in candidates: row["horizon"] = efficient.horizon(row["switch"])
                    selected = efficient.cumulative_rows(selected, candidates, active, look)
                    missing = [key for key in active if len({r["source_block"] for r in selected
                                                             if r["switch"]==key}) < look]
                    # A rare-event pilot uses available banks. Never generate ordinary games
                    # for a switch with no eligible probe/context or <32 qualifying pilot blocks.
                    if look==40 or not missing: break
                    available = {key: len({r["source_block"] for r in candidates if r["switch"]==key})
                                 for key in missing}
                    total_sources = len({r["source_block"] for r in bank.build(directories, [active[0]],
                                                                                 "from-start", 1)})
                    reachable = [key for key in missing if available[key] and
                                 look <= 490 * available[key] / max(1,total_sources)]
                    used = {i for i in range(6) if f"events-{fmt}-batch{i}-opponent5"
                            in self.state["completed_stages"]}
                    if not reachable or len(used)==6: break
                    index = next(i for i in range(6) if i not in used)
                    directories.extend(self.expansion_banks(fmt,index))
            final = self.run_rows(name, selected, keys)
            if not selected:
                stopped.update({key:{"status":"no supported opportunities; unresolved"} for key in active})
                break
            current = efficient.decisions(self.output/name, active, look)
            for key, decision in current.items():
                if look==40 and decision["status"]=="continue":
                    targets[key]=next(n for n in (80,160,320) if n>=decision["required_blocks_90pct"])
                if decision["status"]=="continue" and look>=targets.get(key,320):
                    decision["status"]="unresolved: fixed pilot-sized budget reached"
                decision["budget_blocks"]=targets.get(key)
                count=len({r["source_block"] for r in selected if r["switch"]==key})
                if decision["status"]=="continue" and count<look:
                    decision["status"]="unresolved: opportunity-generation budget exhausted"
                if decision["status"]!="continue": stopped[key]=decision
            active=[key for key in active if key not in stopped]
            all_decisions={**current,**stopped}
            for row in final["summaries"]:
                decision=all_decisions[row["switch"]]
                row.setdefault("descriptive_fixed_sample_ci", [row.get("ci_low"),row.get("ci_high")])
                bounds=decision.get("bounds")
                row["ci_low"],row["ci_high"] = bounds if bounds else (None,None)
                row["verdict"]=decision["status"]
                row["recommendation"]="keep enabled"
                row["fdr_resolved"]=decision["status"]=="harmful; needs fresh confirmation"
                row["sequential_decision"]=decision
            final["sequential_method"]=f"finite-look empirical Bernstein; familywise alpha=.05; 4 looks; {efficient.FAMILIES} tests"
            atomic(self.output/name/"summary.json",final)
            atomic(self.output/(prefix+"-decisions.json"),all_decisions)
            self.report()
        return final

    def confirmation(self, wave: int, fmt: str, summaries: list[dict]):
        promoted = {}
        for summary in summaries:
            for row in summary["summaries"]:
                if row["adequately_powered"] and row["fdr_resolved"] and row["ci_high"] < -row["mpid"]:
                    promoted[row["switch"]] = max(promoted.get(row["switch"], 80),
                                                 row["required_blocks_90pct"])
        if not promoted:
            return
        # A fresh fixed-size from-start full-game test is required before any
        # default change. This controller records evidence but never makes one.
        target = min(320, max(promoted.values()))
        rounds = math.ceil(target/14)
        offset = wave*3000000 + (0 if fmt == "duel" else 1000000)
        confirmation_base = (700000000 + FORMAT_CODES[fmt]*20000000
                             if fmt in FFA_MAPS else 1800000000)
        directories = [self.harvest(f"wave{wave}-{fmt}-confirmation-bank-opponent{ai}",
                                    fmt, rounds, confirmation_base+offset+side*500000,
                                    ai, horizon=1000)
                       for side, ai in enumerate((5, 6))]
        rows = bank.build(directories, list(promoted), "from-start", 180000, quota=target)
        for row in rows:
            row["metric"] = "victory_score"; row["mpid"] = 3.0
        self.run_rows(f"wave{wave}-{fmt}-full-game-confirmation", rows, list(promoted), True)

    def report(self):
        lines = ["# Maxima ablation campaign", "", f"Status: {self.state['status']}",
                 f"Updated: {now()}", "", "Farming and its children remain enabled in the frozen baseline. "
                 "No live defaults are changed by this campaign.", "",
                 "Order: Wave 1 rerun, Wave 2 components, Wave 3 narrow heuristics. "
                 "Remaining stages follow registry FS/CP routing, reuse qualified cumulative runs, "
                 "and use predeclared 40/80/160/320 context looks with conservative bounds. "
                 "Started legacy stages retain their original settings. Fresh full-game confirmation follows promoted harm.", "",
                 "All runs use five remote hosts / 60 slots. Each ON/OFF pair and both repeats have worker affinity. "
                 "Seat rotations are averaged within map/seed blocks; 2v2 scores aggregate allies correctly. "
                 "Duel, FFA3, FFA4, FFA5, and 2v2 are reported separately.", "",
                 "## Results", "",
                 "| Stage | Switch | Blocks | ON−OFF | Verdict |",
                 "|---|---|---:|---:|---|"]
        for entry in self.state["results"]:
            summary = json.loads(Path(entry["summary"]).read_text())
            for row in summary["summaries"]:
                lines.append(f"| {entry['stage']} | `{row['switch']}` | {row['independent_blocks']} | "
                             f"{row['mean_difference']:+.4f} | {row['verdict']} |")
            for switch in entry["missing_switches"]:
                lines.append(f"| {entry['stage']} | `{switch}` | 0 | — | no qualified contexts; unresolved |")
        lines += ["", "## Interpretation limits", "",
                  "A from-start null effect does not establish that a rare behavior had an opportunity. "
                  "Observed-context checkpoints are selected before the first logged context/action, "
                  "but are not certified shadow-choice probes. Missing contexts remain unresolved. "
                  "Context rollouts use the existing fixed 20k horizon; dynamic mission-resolution horizons "
                  "and independent shadow evaluators remain unimplemented parts of the design. "
                  "Full-game confirmation runs to termination or a 180k-tick cap; capped games are not "
                  "reported as completed victories. Stratum estimates are descriptive.", "",
                  "The old farming result used a different binary and the previous 2v2 scoring implementation; "
                  "it is not pooled with these results."]
        (self.output / "CAMPAIGN_REPORT.md").write_text("\n".join(lines)+"\n")

    def run(self):
        qualification = json.loads((self.output / "qualification-ablation/summary.json").read_text())
        if qualification["rejected_checkpoints"]:
            raise RuntimeError("infrastructure qualification has rejected checkpoints")
        self.verify()
        for wave in range(self.lock.get("first_wave", 1), 4):
            if wave > self.lock.get("last_authorized_wave", 3):
                self.save(status="awaiting_wave1_analysis", stage="wave1-completed-awaiting-analysis",
                          child_pid=None, completed_waves=[1], wave1_completed_utc=now())
                self.report()
                print(f"{now()} HOLD: Wave 1 complete; analyze before separately refreezing latest Wave 2 source", flush=True)
                return
            switches = list(ablation.WAVES[wave])
            if wave == 1:
                switches += sorted(bank.MAJOR-set(switches))
            for fmt in FORMATS:
                keys = [k for k in switches if fmt == "2v2" or k not in bank.TEAM_SWITCHES]
                directories = self.discovery_banks(fmt)
                name = f"wave{wave}-{fmt}-from-start"
                if name in self.state["completed_stages"]:
                    rows = ablation.read_manifest(self.output/(name+"-manifest.json"))
                    fs_keys = sorted({row["switch"] for row in rows})
                else:
                    fs_keys = efficient.route(keys, "FS")
                    if wave > 1 and f"wave1-{fmt}-from-start" in self.state["completed_stages"]:
                        # Reuse a major screen only when it ran under this same freeze.
                        fs_keys = [key for key in fs_keys if key not in bank.MAJOR]
                    rows = bank.build(directories, fs_keys, "from-start", 30000, quota=70)
                fs = self.run_rows(name, rows, fs_keys)
                cp = self.conditional(wave, fmt, directories[:], keys)
                self.confirmation(wave, fmt, [fs, cp])
        self.save(status="completed", stage="completed", child_pid=None, completed_utc=now())
        self.report()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--adopt-running-stage", action="store_true",
                        help="Wait for the existing stage process without restarting its simulations")
    args = parser.parse_args()
    if (args.output_dir / "RETIRED.json").exists():
        print("Campaign retired; restart forbidden", file=sys.stderr)
        return 2
    with (args.output_dir / "controller.lock").open("a") as ownership:
        fcntl.flock(ownership, fcntl.LOCK_EX | fcntl.LOCK_NB)
        campaign = Campaign(args.output_dir, args.adopt_running_stage)
        try:
            campaign.run()
        except Exception as exc:
            campaign.save(status="needs_attention", error=str(exc))
            campaign.report()
            traceback.print_exc()
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
