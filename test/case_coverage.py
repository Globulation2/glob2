"""Per-test-case coverage recording for test/select_affected.py.

test/run_tests.py --profile-per-job DIR gives every test job its own
LLVM_PROFILE_FILE; after the job, `finish_job` reduces its raw profiles to the
names of the functions it executed. `write_map` then merges the jobs, asks
llvm-cov once which source file defines each function, and writes the
case -> source files map. Only Clang builds with -fprofile-instr-generate
-fcoverage-mapping produce profiles; see test/run_coverage.py.
"""
from __future__ import annotations

import datetime as dt
import gzip
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def job_environment(directory: Path, index: int) -> str:
    return str(directory / f'job-{index:05d}-%p.profraw')


def executed_functions(show_output: str) -> list[str]:
    """Function names with a nonzero entry count in `llvm-profdata show --all-functions`."""
    names, current = [], None
    for line in show_output.splitlines():
        stripped = line.strip()
        if line.startswith('  ') and not line.startswith('    ') and stripped.endswith(':'):
            current = stripped[:-1]
        elif current and stripped.startswith('Function count:'):
            if int(stripped.split(':', 1)[1]) > 0:
                names.append(current)
            current = None
    return names


def finish_job(directory: Path, index: int, labels: list[str], profdata_tool: str) -> None:
    raws = sorted(directory.glob(f'job-{index:05d}-*.profraw'))
    record = {'labels': labels, 'functions': []}
    if raws:
        merged = directory / f'job-{index:05d}.profdata'
        subprocess.run([profdata_tool, 'merge', '-sparse', *map(str, raws), '-o', str(merged)], check=True,
                       capture_output=True)
        shown = subprocess.run([profdata_tool, 'show', '--all-functions', str(merged)], check=True,
                               capture_output=True, text=True).stdout
        record['functions'] = executed_functions(shown)
        for raw in raws:
            raw.unlink()
    (directory / f'job-{index:05d}.json').write_text(json.dumps(record))


def relative_source(path: str, root: Path = ROOT) -> str | None:
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = (root / candidate).resolve()
    try:
        return candidate.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return None


def function_files(binaries: list[Path], profdata: Path, cov_tool: str, root: Path = ROOT) -> dict[str, list[str]]:
    exported = subprocess.run([cov_tool, 'export', '-format=text', '-skip-expansions',
                               f'-instr-profile={profdata}', str(binaries[0]),
                               *[f'-object={binary}' for binary in binaries[1:]]],
                              check=True, capture_output=True, text=True).stdout
    mapping: dict[str, list[str]] = {}
    for unit in json.loads(exported).get('data', []):
        for function in unit.get('functions', []):
            files = [relative for name in function.get('filenames', []) if (relative := relative_source(name, root))]
            if files:
                mapping[function['name']] = files
    return mapping


def write_map(directory: Path, binaries: list[Path], output: Path, profdata_tool: str, cov_tool: str,
              sha: str | None = None) -> int:
    jobs = sorted(directory.glob('job-*.json'))
    merged = directory / 'all.profdata'
    profiles = [str(path) for path in sorted(directory.glob('job-*.profdata'))]
    if not profiles:
        raise SystemExit('no per-job profiles were recorded; is this a Clang coverage build?')
    subprocess.run([profdata_tool, 'merge', '-sparse', *profiles, '-o', str(merged)], check=True, capture_output=True)
    mapping = function_files(binaries, merged, cov_tool)
    cases: dict[str, list[str]] = {}
    for job in jobs:
        record = json.loads(job.read_text())
        files = sorted({path for function in record['functions'] for path in mapping.get(function, ())})
        for label in record['labels']:
            cases[label] = sorted(set(cases.get(label, [])) | set(files))
    if sha is None:
        sha = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    document = {'schema': 1, 'sha': sha, 'created': dt.datetime.now(dt.timezone.utc).isoformat(), 'cases': cases}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(gzip.compress(json.dumps(document, sort_keys=True).encode(), mtime=0))
    return len(cases)


def combine_maps(paths: list[Path], output: Path) -> None:
    """One map from several binaries' maps (the newest sha and time win)."""
    documents = [json.loads(gzip.decompress(path.read_bytes())) for path in paths]
    cases = {}
    for document in documents:
        cases.update(document['cases'])
    combined = {'schema': 1, 'sha': documents[-1]['sha'], 'created': documents[-1]['created'], 'cases': cases}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(gzip.compress(json.dumps(combined, sort_keys=True).encode(), mtime=0))
