#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""remove-unused-includes.py — strip unused #includes from src/, compile-gated.

Drives clang-tidy's misc-include-cleaner (Homebrew LLVM) off compile_commands.json,
keeps only the "included header X is not used directly" removal fix-its, applies
them per file with clang-apply-replacements, and syntax-checks every edited file
with its own compile flags before keeping the edit. Files whose full removal set
fails the gate are retried one include at a time so the redundant includes still
go while the load-bearing one stays.

Why the gate is essential: misc-include-cleaner reports per translation unit and
flags a header as "not used directly" even when it is the sole transitive provider
of a symbol the file uses. Deleting that header fails to compile. The gate also
runs every file that compiles under -DYOG_SERVER_ONLY through that config, so
includes used only by the server build survive.

The IWYU-style "no header providing Y is directly included" insertions are turned
off at the source (MissingIncludes: false) — adding direct includes is churn with
no porting benefit. Two headers are never removed: config.h only carries
build-configuration macros, and -fsyntax-only cannot tell whether a downstream
#ifdef changed meaning; SDL.h is the umbrella every SDL_* call is meant to come
through, and the cleaner would swap it for whatever sub-header happens to arrive
transitively.

Scope is src/ only. libgag/ and libusl/ are vendored (see cpp-bugs/CLAUDE.md).

Usage:
  scons compile_commands.json          # refresh the compile database first
  tools/remove-unused-includes.py [--jobs N] [--only REGEX] [--dry-run]

Afterwards:
  scons -j16 && scons server=1 -j16 && scons server=0 -j16
  then Workflow 1 in ../docs/replay-verification.md (G2 plus the gradient corpus).
  Replay equality is the only check that catches the one risk -fsyntax-only misses:
  a removed header's static-initializer side effect.
"""

import argparse
import collections
import concurrent.futures
import glob
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

import yaml

CHECK = "misc-include-cleaner"
REMOVAL_MESSAGE = "is not used directly"
CHECK_CONFIG = (
    "{CheckOptions: {"
    "misc-include-cleaner.MissingIncludes: false, "
    "misc-include-cleaner.IgnoreHeaders: 'config\\.h;SDL\\.h'}}"
)
SERVER_DEFINE = "-DYOG_SERVER_ONLY"


def find_llvm_bin(explicit):
    candidates = [explicit] if explicit else []
    candidates += sorted(glob.glob("/opt/homebrew/opt/llvm*/bin"), reverse=True)
    candidates += sorted(glob.glob("/usr/local/opt/llvm*/bin"), reverse=True)
    for d in candidates:
        if d and all(os.path.exists(os.path.join(d, t))
                     for t in ("clang-tidy", "clang-apply-replacements")):
            return d
    sys.exit("clang-tidy + clang-apply-replacements not found; "
             "brew install llvm, or pass --llvm-bin")


def macos_sdk():
    if sys.platform != "darwin":
        return None
    return subprocess.run(["xcrun", "--show-sdk-path"], check=True,
                          capture_output=True, text=True).stdout.strip()


def load_compile_commands(root, only):
    path = os.path.join(root, "compile_commands.json")
    if not os.path.exists(path):
        sys.exit("compile_commands.json missing — run: scons compile_commands.json")
    with open(path) as f:
        entries = json.load(f)
    commands = {}
    for e in entries:
        rel = os.path.relpath(os.path.join(e.get("directory", root), e["file"]), root)
        if not rel.startswith("src/") or not rel.endswith((".cpp", ".cc", ".cxx")):
            continue
        if only and not re.search(only, rel):
            continue
        commands.setdefault(rel, e.get("command") or shlex.join(e["arguments"]))
    present = {p for p in glob.glob("src/**/*.cpp", recursive=True, root_dir=root)}
    stale = [f for f in commands if f not in present]
    if stale:
        sys.exit("compile_commands.json lists files that no longer exist "
                 f"({stale[:3]}...) — run: scons compile_commands.json")
    return commands


def syntax_check_argv(command, extra=()):
    """The file's own compile command with codegen dropped: -o stripped, -c → -fsyntax-only."""
    argv = shlex.split(command)
    out = []
    skip = False
    for a in argv:
        if skip:
            skip = False
            continue
        if a == "-o":
            skip = True
        elif a == "-c":
            out.append("-fsyntax-only")
        elif a == "-s":
            continue
        else:
            out.append(a)
    return out + list(extra)


def run_quiet(argv, cwd):
    return subprocess.run(argv, cwd=cwd, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0


def detect(llvm_bin, sdk, root, rel, fixes_dir):
    yaml_path = os.path.join(fixes_dir, rel.replace("/", "__") + ".yaml")
    argv = [os.path.join(llvm_bin, "clang-tidy"), "-p", root,
            f"--checks=-*,{CHECK}", f"--config={CHECK_CONFIG}",
            f"--export-fixes={yaml_path}", "--quiet"]
    if sdk:
        argv += ["--extra-arg-before=-isysroot", f"--extra-arg-before={sdk}"]
    proc = subprocess.run(argv + [rel], cwd=root, capture_output=True, text=True)
    if "error:" in proc.stderr or "error:" in proc.stdout:
        return rel, None, (proc.stdout + proc.stderr).strip()
    if not os.path.exists(yaml_path):
        return rel, [], None
    with open(yaml_path) as f:
        doc = yaml.safe_load(f) or {}
    removals = []
    for d in doc.get("Diagnostics", []):
        msg = d["DiagnosticMessage"]
        if d["DiagnosticName"] != CHECK or REMOVAL_MESSAGE not in msg["Message"]:
            continue
        for r in msg.get("Replacements", []):
            r_rel = os.path.relpath(os.path.join(root, r["FilePath"]), root)
            if r_rel != rel or r["ReplacementText"] != "":
                continue
            removals.append({"Offset": r["Offset"], "Length": r["Length"],
                             "Header": msg["Message"].split()[2]})
    return rel, removals, None


def apply_replacements(llvm_bin, root, rel, removals):
    with tempfile.TemporaryDirectory(prefix="uic-apply-") as d:
        doc = {
            "MainSourceFile": os.path.join(root, rel),
            "Diagnostics": [{
                "DiagnosticName": CHECK,
                "DiagnosticMessage": {
                    "Message": f"included header {r['Header']} {REMOVAL_MESSAGE}",
                    "FilePath": os.path.join(root, rel),
                    "FileOffset": r["Offset"],
                    "Replacements": [{"FilePath": os.path.join(root, rel),
                                      "Offset": r["Offset"], "Length": r["Length"],
                                      "ReplacementText": ""}],
                },
                "Level": "Warning",
                "BuildDirectory": root,
            } for r in removals],
        }
        with open(os.path.join(d, "fixes.yaml"), "w") as f:
            yaml.safe_dump(doc, f)
        subprocess.run([os.path.join(llvm_bin, "clang-apply-replacements"), d],
                       cwd=root, check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)


def gate(root, rel, command, removals, llvm_bin, dry_run):
    """Apply removals to one file; keep only what still passes -fsyntax-only.

    Returns (kept, rejected) lists of removal dicts."""
    path = os.path.join(root, rel)
    with open(path, "rb") as f:
        original = f.read()
    default_argv = syntax_check_argv(command)
    if not run_quiet(default_argv, root):
        return [], removals, "does not compile before any edit; skipped"
    configs = [default_argv]
    if run_quiet(syntax_check_argv(command, [SERVER_DEFINE]), root):
        configs.append(syntax_check_argv(command, [SERVER_DEFINE]))

    def passes():
        return all(run_quiet(argv, root) for argv in configs)

    def restore(content):
        with open(path, "wb") as f:
            f.write(content)

    if dry_run:
        return removals, [], None

    apply_replacements(llvm_bin, root, rel, removals)
    if passes():
        return removals, [], None
    restore(original)

    # Bottom-up (descending offset) so unprocessed offsets stay valid.
    kept, rejected = [], []
    for r in sorted(removals, key=lambda r: r["Offset"], reverse=True):
        with open(path, "rb") as f:
            snapshot = f.read()
        apply_replacements(llvm_bin, root, rel, [r])
        if passes():
            kept.append(r)
        else:
            restore(snapshot)
            rejected.append(r)
    return kept, rejected, None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--only", help="regex on the src-relative path; default all of src/")
    ap.add_argument("--llvm-bin", help="directory containing clang-tidy")
    ap.add_argument("--dry-run", action="store_true", help="report, do not edit")
    args = ap.parse_args()

    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    llvm_bin = find_llvm_bin(args.llvm_bin)
    sdk = macos_sdk()
    commands = load_compile_commands(root, args.only)
    print(f"{len(commands)} translation units; clang-tidy from {llvm_bin}")

    fixes_dir = tempfile.mkdtemp(prefix="uic-fixes-")
    findings = {}
    errors = {}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(detect, llvm_bin, sdk, root, rel, fixes_dir) for rel in commands]
        for fut in concurrent.futures.as_completed(futures):
            rel, removals, err = fut.result()
            if err:
                errors[rel] = err
            elif removals:
                findings[rel] = removals
    shutil.rmtree(fixes_dir)
    for rel, err in sorted(errors.items()):
        print(f"ANALYSIS FAILED {rel}:\n  " + err.splitlines()[0])
    total = sum(len(v) for v in findings.values())
    print(f"{total} removal candidates across {len(findings)} files")

    results = {}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(gate, root, rel, commands[rel], removals, llvm_bin, args.dry_run): rel
                   for rel, removals in findings.items()}
        for fut in concurrent.futures.as_completed(futures):
            results[futures[fut]] = fut.result()

    kept_total = 0
    rejected = collections.Counter()
    for rel in sorted(results):
        kept, rej, note = results[rel]
        kept_total += len(kept)
        if note:
            print(f"SKIPPED {rel}: {note}")
        for r in rej:
            rejected[r["Header"]] += 1
            print(f"KEPT (load-bearing) {rel}: {r['Header']}")
    verb = "would remove" if args.dry_run else "removed"
    print(f"{verb} {kept_total} includes across "
          f"{sum(1 for k, _, _ in results.values() if k)} files; "
          f"{sum(rejected.values())} flagged includes kept by the compile gate")
    if errors:
        sys.exit(1)


if __name__ == "__main__":
    main()
