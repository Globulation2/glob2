#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reuse a completed release benchmark's exact compile/link inputs for fixture export."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-log', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/renderer-feasibility')
    args = parser.parse_args()
    lines = args.build_log.read_text().splitlines()
    compile_command = link_command = None
    for line in lines:
        if not line.startswith(('g++ ', 'clang++ ', 'c++ ')):
            continue
        command = shlex.split(line)
        if '-o' not in command:
            continue
        destination = command[command.index('-o') + 1]
        if '-c' in command and any(p.endswith('src/render/torus/TorusRenderBenchmark.cpp') for p in command):
            compile_command = command
        if '-c' not in command and Path(destination).name == 'torus-render-benchmark':
            link_command = command
    if not compile_command or not link_command:
        parser.error('Need the complete release torus benchmark compilation and linking commands')
    if '-O3' not in compile_command or any(p in compile_command for p in ('-O0', '-g1')):
        parser.error('The recorded benchmark must be a release build')
    args.output.mkdir(parents=True, exist_ok=True)
    old_object = compile_command[compile_command.index('-o') + 1]
    new_object = str((args.output / 'export-fixture.o').resolve())
    binary = str((args.output / 'export-fixture').resolve())
    compile_command[compile_command.index('-o') + 1] = new_object
    compile_command = [str(ROOT / 'tools/rendering/export_fixture.cpp') if p.endswith('src/render/torus/TorusRenderBenchmark.cpp') else p for p in compile_command]
    link_command = [new_object if p == old_object else p for p in link_command]
    link_command[link_command.index('-o') + 1] = binary
    report = {'compile_command': compile_command, 'link_command': link_command}
    for label, command in [('compile', compile_command), ('link', link_command)]:
        run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        (args.output / ('fixture-exporter-' + label + '.log')).write_text(run.stdout + run.stderr)
        report[label] = run.returncode
        if run.returncode:
            break
    (args.output / 'fixture-exporter-build.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(any(report.get(k, 0) for k in ('compile', 'link')))

if __name__ == '__main__':
    raise SystemExit(main())
