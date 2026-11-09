#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Structured native map requests shared by local generator studies.

Resolve generators from one catalog per study, then submit independent requests. Refusals,
process failures, timeouts and malformed artifacts remain distinct. No stdout text is a schema.
The native command isolates its profile beneath the temporary output directory.
"""
import atexit
import json
import math
import os
import queue
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def default_binary():
    """The newest built client (perf builds included), with a note when it is a dev build."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from build_paths import study_binary
    binary, optimized = study_binary()
    if not optimized:
        print(f'note: using the -O0 dev build {binary}; python3 tools/dev_build.py perf=1 '
              'generates several times faster', file=sys.stderr)
    return str(binary)


def default_jobs():
    """One process per logical CPU: map generation is CPU bound and single-threaded."""
    return os.cpu_count() or 1


def child_threads(jobs):
    """--compute-threads for each of `jobs` concurrent processes, so together they
    fill the machine without oversubscribing it (candidate searches and reports
    inside one process fork/join on this many threads)."""
    return max(1, (os.cpu_count() or 1) // max(1, jobs))


def load_catalog(binary, timeout=60, generator_packages=()):
    result = subprocess.run([str(binary), 'info', 'catalog', '--format', 'json']+[v for p in generator_packages for v in ['--generator-package',str(p)]], capture_output=True,
                            text=True, timeout=timeout, check=True)
    catalog = json.loads(result.stdout)
    if catalog.get('schema_version') != 1 or not isinstance(catalog.get('generators'), list):
        raise ValueError('Unsupported generator catalog schema')
    return catalog


def generator_definition(catalog, name):
    for definition in catalog['generators']:
        if str(name) in (definition['id'], str(definition['method'])):
            return definition
    raise ValueError(f'Unknown generator: {name}')


def dimension_exponent(tiles):
    if not isinstance(tiles, int) or tiles < 1 or tiles & (tiles - 1):
        raise ValueError('Map dimensions must be positive powers of two')
    return tiles.bit_length() - 1


class _StudyWorker:
    """One `glob2 map study-worker` process: studies run back to back in it, so a sweep pays
    process start and setup once per worker rather than once per map."""
    DONE = 'glob2-study-worker done '

    def __init__(self, binary, generator_packages, compute_threads):
        command = [str(binary), 'map', 'study-worker']
        command += [v for p in generator_packages for v in ['--generator-package', str(p)]]
        if compute_threads:
            command += ['--compute-threads', str(compute_threads)]
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.lines = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.process.stdout:
            if line.startswith(self.DONE):
                self.lines.put(int(line[len(self.DONE):]))
        self.lines.put(None)  # The worker exited.

    def run(self, arguments, timeout):
        """The study's exit code; raises TimeoutExpired (the worker is then killed) or
        ChildProcessError when the worker died during the study."""
        try:
            self.process.stdin.write(json.dumps(arguments) + '\n')
            self.process.stdin.flush()
        except OSError as error:
            raise ChildProcessError(f'study worker unavailable: {error}') from error
        try:
            code = self.lines.get(timeout=timeout)
        except queue.Empty:
            self.close()
            raise subprocess.TimeoutExpired(arguments, timeout) from None
        if code is None:
            self.process.wait()
            raise ChildProcessError(f'study worker exited with {self.process.returncode}')
        return code

    def alive(self):
        return self.process.poll() is None

    def close(self):
        if self.alive():
            self.process.kill()
        self.process.wait()


_workers = threading.local()
_all_workers = []
_workers_lock = threading.Lock()
_capable = {}


def _supports_worker(binary):
    """Whether this binary has `map study-worker` (and so --compute-threads on map
    commands): older builds, such as a master baseline, run one process per map."""
    binary = str(binary)
    with _workers_lock:
        if binary not in _capable:
            try:
                probe = subprocess.run([binary, 'map', 'study-worker'], stdin=subprocess.DEVNULL,
                                       capture_output=True, timeout=60)
                _capable[binary] = probe.returncode == 0
            except (OSError, subprocess.TimeoutExpired):
                _capable[binary] = False
        return _capable[binary]


@atexit.register
def _close_workers():
    for worker in _all_workers:
        if worker.alive():
            worker.process.stdin.close()
            worker.close()


def _run_study(binary, arguments, generator_packages, compute_threads, timeout):
    """Run one `map study` (its arguments after the binary) and return its exit code.

    A persistent worker per calling thread runs the study when the binary supports it and
    GLOB2_STUDY_WORKER is not 0; otherwise, or for a binary without the command, a fresh
    process does. Results are identical either way: each study is the same command."""
    capable = _supports_worker(binary)
    if not capable:
        compute_threads = None
    key = (str(binary), tuple(map(str, generator_packages)), compute_threads)
    if capable and os.environ.get('GLOB2_STUDY_WORKER', '1') != '0':
        workers = getattr(_workers, 'by_key', None)
        if workers is None:
            workers = _workers.by_key = {}
        worker = workers.get(key)
        if worker is None or not worker.alive():
            worker = workers[key] = _StudyWorker(binary, generator_packages, compute_threads)
            with _workers_lock:
                _all_workers.append(worker)
        try:
            return worker.run(arguments, timeout)
        except ChildProcessError:
            return -1  # A crash in this study; the next call starts a fresh worker.
    command = [str(binary), *arguments]
    command += [v for p in generator_packages for v in ['--generator-package', str(p)]]
    if compute_threads:
        command += ['--compute-threads', str(compute_threads)]
    return subprocess.run(command, capture_output=True, text=True, timeout=timeout).returncode


def generate_map(binary, method, seed, width, height, teams, settings=None, timeout=120, generator_packages=(),
                 compute_threads=None):
    """Return category (completed/refused/execution_error), detail, timing and native JSON.

    This is a single seed, without lobby candidate selection. Dimensions are tile counts;
    conversion to the native API's exponents happens only here. Caller configuration errors
    raise; engine-side invalid requests are retained as refusals with their native status.
    """
    params = dict(width=dimension_exponent(width), height=dimension_exponent(height), teams=teams)
    if set(settings or {}) & {'width', 'height', 'teams'}:
        raise ValueError('Dimensions and teams must be supplied as request arguments')
    params.update(settings or {})
    if timeout <= 0:
        raise ValueError('Timeout must be positive')
    started = time.monotonic()
    row = dict(category='execution_error', detail='', returncode=None, native=None)
    with tempfile.TemporaryDirectory(prefix='glob2-map-study-') as directory:
        arguments = ['map', 'study', str(method), '--seed', str(seed), '--output-dir', directory]
        for key, value in params.items():
            arguments += ['--set', f'{key}={value}']
        try:
            returncode = _run_study(binary, arguments, generator_packages, compute_threads, timeout)
            row['returncode'] = returncode
            native = json.loads((Path(directory) / 'result.json').read_text())
            if native.get('schema_version') != 1:
                raise ValueError('Unsupported native result schema')
            row['native'] = native
            status = native.get('status')
            if status == 'completed' and returncode == 0:
                report = native.get('map_report')
                if (not isinstance(report, dict) or
                        report.get('generation', {}).get('outcome', {}).get('success') is not True):
                    raise ValueError('Completed result has no successful map report')
                validate_map_report(report)
                row['category'] = 'completed'
            elif (status, returncode) in {('generation_failed', 4), ('invalid_request', 2)}:
                row['category'] = 'refused'
            row['detail'] = native.get('diagnostic', '')
            if row['category'] == 'execution_error':
                row['detail'] = row['detail'] or f'Native status {status!r}, exit {returncode}'
        except subprocess.TimeoutExpired:
            row['detail'] = f'Timed out after {timeout:g} seconds'
        except (OSError, ValueError, TypeError, AttributeError, KeyError) as error:
            row['detail'] = f'Execution/result error: {error}'
    row['seconds'] = round(time.monotonic() - started, 3)
    return row


def finite_number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Expected a finite numeric map measurement')
    return value


def validate_map_report(report):
    """Validate the fields consumed by all local studies before classifying a result as complete."""
    for name in ('grass', 'sand', 'water'):
        value = finite_number(report['terrain'][name]['percent'])
        if not 0 <= value <= 100:
            raise ValueError('Terrain percentage is out of range')
    for name in ('wheat', 'wood', 'stone', 'algae', 'cherry', 'orange', 'prune'):
        count = finite_number(report['resources']['types'][name]['coverage']['tiles'])
        if count < 0:
            raise ValueError('Negative resource coverage')
    if finite_number(report['space']['build_sites_4x4']) < 0:
        raise ValueError('Negative building capacity')
    if finite_number(report['space']['land_regions']['components']) < 0:
        raise ValueError('Negative component count')
    finite_number(report['fertility']['all_tiles']['mean'])
    quality = report['generation'].get('selection_quality') or {}
    for key in ('score', 'worst_fitness'):
        if quality.get(key) is not None:
            finite_number(quality[key])
    report_metrics(report)


def report_metrics(report):
    """Keep compatibility means plus extrema; retain raw subject-labelled telemetry separately."""
    measurements = {}
    records = (report.get('generation', {}).get('telemetry') or {}).get('records', [])
    for record in records:
        value = record.get('value')
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            measurements.setdefault(record['key'], []).append(finite_number(value))
    metrics = {}
    for key, values in measurements.items():
        metrics['tel:' + key] = sum(values) / len(values)
        metrics['tel-min:' + key] = min(values)
        metrics['tel-max:' + key] = max(values)
    if report.get('generation', {}).get('outcome', {}).get('success'):
        for key, value in report['terrain'].items():
            if isinstance(value, dict):
                metrics['terrain%:' + key] = value['percent']
        for key, value in report['resources']['types'].items():
            metrics['tiles:' + key] = value['coverage']['tiles']
        metrics['sites4x4'] = report['space']['build_sites_4x4']
        metrics['fertility'] = report['fertility']['all_tiles']['mean']
    return metrics, records
