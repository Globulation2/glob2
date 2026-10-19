#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Resumable unified research campaigns; supplied build hooks, no stable promotion.

Run with an explicit frozen configuration. Engines use the ordinary tournament
coordinator. Hooks are synchronous trusted argv adapters, never shell strings.
The driver emits staged coefficients; a build/qualification receipt is required
before those coefficients can supply the reserved candidate games.
"""
import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from tools import adaptive_gradient_dataset as data
from tools import adaptive_gradient_export as exporter
from tools import adaptive_gradient_model as model
from tools import adaptive_gradient_spatial as spatial
from tools import adaptive_gradient_tournament as tournament
from tools import resource_gradient_model as generator
from tools.resource_gradient_tournament import digest, map_identity
from tools.tournaments.bundles import inspect_bundle, package_identity
from tools.tournaments.cli import configure_hosts
from tools.tournaments.common import atomic_json, file_hash, lock
from tools.tournaments.coordinator import Coordinator


def read(path):
    return json.loads(Path(path).read_text())


def checked_files(paths):
    return {str(Path(p).resolve()): file_hash(p) for p in paths}


def verify_files(files):
    for path, sha in files.items():
        if not Path(path).is_file() or file_hash(path) != sha:
            raise ValueError('Frozen stage input/output changed: ' + path)


def tool_identity():
    # Changing an orchestrator, contract, fitter or worker package requires a new
    # campaign directory. Active campaigns never resume under edited collectors.
    names = ['adaptive_gradient_campaign.py', 'adaptive_gradient_tournament.py',
             'adaptive_gradient_export.py', 'adaptive_gradient_dataset.py',
             'adaptive_gradient_model.py', 'adaptive_gradient_codegen.py',
             'adaptive_gradient_spatial.py', 'resource_gradient_model.py',
             'resource_gradient_dataset.py', 'resource_gradient_distributed.py',
             'resource_gradient_tournament.py', 'resource_gradient_experiments.py']
    return dict(worker=package_identity(), tools={n: file_hash(ROOT / 'tools' / n) for n in names})


def terminal_accounting(value):
    counts = value['counts']
    if (value['expected'] != 128 or sum(counts.values()) != 128
            or set(counts) - {'completed', 'user-censored'}):
        raise ValueError('Require all128 slots completed or explicitly user-censored; no retries or omissions')
    return value['completed_manifests'] + value['censored_manifests']


def cohort_files(manifests):
    paths = list(manifests)
    for manifest in manifests:
        records, _ = data.manifest_records(manifest)
        for record in records:
            episode = Path(record['episodes'])
            episode = episode if episode.is_absolute() else Path(manifest).parent / episode
            if file_hash(episode) != record['episode_sha256']:
                raise ValueError('Cohort episode bytes changed')
            paths.append(episode)
    return paths


def validate_bundle_set(bundles):
    """Inspect supplied metadata before map generation or job creation."""
    if not bundles:
        raise ValueError('Nonempty ordinary unified bundle set required')
    identities = set()
    for bundle in bundles:
        options = bundle['build_options']
        hashes = [options.get('model_sha256'), options.get('header_sha256')]
        if (any(not isinstance(h, str) or len(h) != 64 or any(v not in '0123456789abcdef' for v in h) for h in hashes)
                or options.get('adaptive_gradient_contract_sha256') != data.canonical_hash(spatial.CONTRACT)
                or options.get('model_contract') != data.CONTRACT_ID
                or options.get('collector_contract') != spatial.CONTRACT_ID
                or type(options.get('unified_model_trained')) is not bool
                or options.get('release') != 1 or options.get('optimization') != 'O3'
                or options.get('dev_fast') != 0 or options.get('pch') != 0 or options.get('unity') != 0):
            raise ValueError('Supplied bundles require ordinary O3 release and exact unified model/collector contract')
        if not bundle.get('source_revision'):
            raise ValueError('Supplied bundle source revision required')
        identities.add((bundle['source_revision'], *hashes, options['unified_model_trained']))
    if len(identities) != 1:
        raise ValueError('Supplied bundles must agree on source, model, header and trained state')


def qualification(receipt, emitted, header):
    """Receipt and actual immutable bundles must agree, not just a trained flag."""
    if (receipt.get('status') != 'qualified-research' or receipt.get('stable_promotion') is not False
            or receipt.get('model_sha256') != file_hash(emitted)
            or receipt.get('header_sha256') != file_hash(header)
            or receipt.get('collector_contract_sha256') != data.canonical_hash(spatial.CONTRACT)):
        raise ValueError('Build hook did not qualify these exact research coefficients/collector')
    for check in ('integer_parity', 'determinism_smoke', 'simulation_gates'):
        if receipt.get('checks', {}).get(check) is not True:
            raise ValueError('Missing qualification check: ' + check)
    paths = receipt.get('bundles', [])
    if not paths or len(set(paths)) != len(paths):
        raise ValueError('Explicit unique qualified bundle paths required')
    bundles = [inspect_bundle(p) for p in paths]
    validate_bundle_set(bundles)
    revisions = {b['source_revision'] for b in bundles}
    if len(revisions) != 1 or None in revisions:
        raise ValueError('Qualified bundles must use one actual source revision')
    binaries = receipt.get('binaries', [])
    for bundle in bundles:
        options = bundle['build_options']
        if (options.get('model_sha256') != receipt['model_sha256']
                or options.get('header_sha256') != receipt['header_sha256']
                or options.get('adaptive_gradient_contract_sha256') != receipt['collector_contract_sha256']
                or options.get('unified_model_trained') is not True
                or options.get('release') != 1 or options.get('dev_fast') != 0
                or options.get('pch') != 0 or options.get('unity') != 0
                or options.get('optimization') != 'O3'):
            raise ValueError('Qualified bundle contract/build flags differ')
        sha = next(f['sha256'] for f in bundle['files'] if f['path'] == bundle['executable'])
        if dict(binary_sha256=sha, source_revision=bundle['source_revision'], platform=bundle['platform']) not in binaries:
            raise ValueError('Actual binary lacks a qualification receipt')
    return paths


class Campaign:
    def __init__(self, config, directory):
        self.config_path = Path(config).resolve()
        self.config = read(self.config_path)
        self.base = self.config_path.parent
        self.root = Path(directory).resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        c = self.config
        required = {'schema', 'catalog', 'bootstrap_bundles', 'hosts', 'host_pools',
                    'split_registry', 'expected_families', 'routing', 'rounds', 'build_hook'}
        optional = {'map_generator_bundle', 'prior_manifests', 'fit_hook', 'spatial_analysis', 'intervals'}
        if set(c) - required - optional or required - set(c) or c.get('schema') != 1:
            raise ValueError('Explicit campaign schema1 configuration required; unknown keys rejected')
        if c['routing'] not in tournament.ROUTING or not c['rounds']:
            raise ValueError('Explicit routing and nonempty finite rounds required')
        if not isinstance(c['expected_families'], list) or any(not isinstance(x, str) for x in c['expected_families']):
            raise ValueError('Explicit expected family list required')
        model.expected_family_scope(','.join(c['expected_families']))
        seeds = []
        for round_config in c['rounds']:
            if set(round_config) - {'seed', 'accepted_maps'} or type(round_config.get('seed')) is not int:
                raise ValueError('Round requires seed and optional accepted_maps only')
            seeds.append(round_config['seed'])
            if not 0 <= round_config['seed'] <= 2**32 - 1:
                raise ValueError('Round seed must be uint32')
            if not round_config.get('accepted_maps') and not c.get('map_generator_bundle'):
                raise ValueError('Supply accepted maps or an immutable central map-generator bundle')
        if len(set(seeds)) != len(seeds):
            raise ValueError('Round seeds must be distinct')
        for name in ('build_hook', 'fit_hook'):
            if name in c:
                hook = c[name]
                if (set(hook) != {'argv', 'inputs'} or not isinstance(hook['argv'], list)
                        or not hook['argv'] or any(not isinstance(a, str) for a in hook['argv'])
                        or not isinstance(hook['inputs'], list) or any(not isinstance(a, str) for a in hook['inputs'])
                        or any(token not in ' '.join(hook['argv']) for token in ('{request}', '{receipt}'))):
                    raise ValueError('Hooks require trusted argv list and explicit input-file list')
        self.hosts = configure_hosts(read(self.path(c['hosts'])))
        for host in self.hosts:
            platform = host.get('platform')
            if (not isinstance(platform, dict) or set(platform) != {'os', 'arch'}
                    or any(not isinstance(v, str) or not v for v in platform.values())):
                raise ValueError('Campaign hosts require explicit platform {os,arch} before any job creation: ' + host['name'])
        if set(c['host_pools']) != {h['name'] for h in self.hosts}:
            raise ValueError('Declare execution pools for every supplied host')
        pools = {'small-pharaoh', 'middle-linux'} | ({'large-mac'} if c['routing'] == 'with-mac' else set())
        if any(not isinstance(v, list) or not v or set(v) - pools for v in c['host_pools'].values()):
            raise ValueError('Hosts require explicit nonempty known execution-pool lists')
        self.bundle_paths = [str(self.path(p)) for p in c['bootstrap_bundles']]
        validate_bundle_set([inspect_bundle(p) for p in self.bundle_paths])
        inputs = [self.config_path, self.path(c['catalog']), self.path(c['hosts']), self.path(c['split_registry'])]
        inputs += cohort_files([self.path(p) for p in c.get('prior_manifests', [])])
        inputs += [self.path(r['accepted_maps']) for r in c['rounds'] if r.get('accepted_maps')]
        for name in ('build_hook', 'fit_hook'):
            inputs += [self.path(p) for p in c.get(name, {}).get('inputs', [])]
        inputs += [Path(p) / 'bundle.json' for p in self.bundle_paths]
        if c.get('map_generator_bundle'):
            inspect_bundle(self.path(c['map_generator_bundle']))
            inputs.append(self.path(c['map_generator_bundle']) / 'bundle.json')
        identity = dict(config=c, inputs=checked_files(inputs), tools=tool_identity())
        self.state_path = self.root / 'campaign.json'
        if self.state_path.exists():
            self.state = read(self.state_path)
            if self.state['identity'] != identity:
                raise ValueError('Campaign configuration, tools or frozen inputs changed; use a new directory')
        else:
            self.state = dict(schema=1, identity=identity, stages={}, stable_promotion=False)
            atomic_json(self.state_path, self.state)

    def path(self, value):
        p = Path(value)
        return p.resolve() if p.is_absolute() else (self.base / p).resolve()

    def stage(self, name, work):
        saved = self.state['stages'].get(name)
        if saved:
            verify_files(saved['files'])
            return saved['result']
        self.state['active_stage'] = name
        atomic_json(self.state_path, self.state)
        result, paths = work()
        self.state['stages'][name] = dict(result=result, files=checked_files(paths))
        self.state.pop('active_stage', None)
        atomic_json(self.state_path, self.state)
        return result

    def hook(self, name, request, directory):
        directory.mkdir(parents=True, exist_ok=True)
        request_path, receipt_path = directory / 'request.json', directory / 'receipt.json'
        atomic_json(request_path, request)
        request_sha = file_hash(request_path)
        running = directory / 'running.json'
        success = directory / 'success.json'
        replacements = dict(request=str(request_path), receipt=str(receipt_path), directory=str(directory))
        argv = [part.format_map(replacements) for part in self.config[name]['argv']]
        argv_sha = digest(argv)
        # A receipt is often written before the child exits. Never infer process
        # completion from it, nor from a PID that may have been reused. An orphaned
        # attempt requires explicit inspection/recovery even if its PID is gone.
        if running.exists():
            raise ValueError('Prior hook completion is ambiguous; inspect/recover the recorded attempt before resuming')
        if success.exists():
            marker = read(success)
            if (marker != dict(request_sha256=request_sha, receipt_sha256=file_hash(receipt_path), argv_sha256=argv_sha, exit_code=0)
                    or read(receipt_path).get('request_sha256') != request_sha):
                raise ValueError('Hook success marker differs from frozen request/receipt/argv')
            return read(receipt_path)
        if receipt_path.exists():
            raise ValueError('Hook receipt has no successful child-exit marker; explicit recovery required')
        atomic_json(directory / 'command.json', argv)
        # Publish intent before spawning. Death between Popen and recording its
        # PID must not leave an untracked child that resume can duplicate.
        atomic_json(running, dict(state='launch-intent', request_sha256=request_sha, argv_sha256=argv_sha))
        with (directory / 'stdout.log').open('a') as out, (directory / 'stderr.log').open('a') as err:
            process = subprocess.Popen(argv, cwd=self.base, stdout=out, stderr=err)
            atomic_json(running, dict(state='running', pid=process.pid, request_sha256=request_sha, argv_sha256=argv_sha))
            code = process.wait()
        if code:
            raise ValueError(f'{name} failed with exit{code}; inspect {directory}')
        if not receipt_path.exists() or read(receipt_path).get('request_sha256') != request_sha:
            raise ValueError('Synchronous hook must return a matching durable receipt')
        atomic_json(success, dict(request_sha256=request_sha, receipt_sha256=file_hash(receipt_path), argv_sha256=argv_sha, exit_code=0))
        running.unlink()
        return read(receipt_path)

    def design(self, directory, spec, previous_registry):
        directory.mkdir(parents=True, exist_ok=True)
        document = tournament.plan(read(self.path(self.config['catalog'])), spec['seed'], self.config['routing'])
        atomic_json(directory / 'plan.json', document)
        registry = directory / 'map-splits.json'
        # Reserve every predeclared fallback before observing generator outcomes.
        possible = []
        for slot in document['unified']['slots']:
            base = {k: v for k, v in slot['spec'].items() if k != 'map_candidates'}
            possible += [dict(spec=base), *[dict(spec=dict(base, **v)) for v in slot['spec'].get('map_candidates', [])]]
        # Reconstruct from frozen prior exclusions, not a provisional file left
        # by an interrupted attempt. Never legitimize altered held-out folds.
        with tempfile.TemporaryDirectory() as tmp:
            expected_registry = Path(tmp) / 'map-splits.json'
            shutil.copyfile(previous_registry, expected_registry)
            tournament.freeze_splits(possible, expected_registry)
            expected = read(expected_registry)
        if registry.exists() and read(registry) != expected:
            raise ValueError('Provisional map split registry differs from frozen previous assignments')
        atomic_json(registry, expected)
        if spec.get('accepted_maps'):
            records = read(self.path(spec['accepted_maps']))
            records = records.get('maps', records) if isinstance(records, dict) else records
            records = copy.deepcopy(records)
            for r in records:
                p = Path(r['path'])
                r['path'] = str(p.resolve() if p.is_absolute() else (self.path(spec['accepted_maps']).parent / p).resolve())
        else:
            bundle = inspect_bundle(self.path(self.config['map_generator_bundle']))
            records = []
            old_root = generator.ROOT
            try:
                generator.ROOT = Path(bundle['directory'])
                for slot in document['unified']['slots']:
                    out = directory / 'maps' / slot['group']
                    out.mkdir(parents=True, exist_ok=True)
                    saved = out / 'accepted.json'
                    if saved.exists():
                        record = read(saved)
                        if (record['planned_spec_sha256'] != digest(slot['spec']) or record.get('mapgen_bundle') != bundle['id']
                                or file_hash(record['path']) != record['sha256']):
                            raise ValueError('Generated map resume identity changed')
                    else:
                        env = {k: v for k, v in os.environ.items() if not k.startswith('GLOB2_')}
                        env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                                   GLOB2_USER_DATA_DIR=str(out / 'profile'), GLOB2_USER_DIR=str(out / 'profile'))
                        private = Path(bundle['directory']) / 'lib/glob2'
                        if private.exists(): env['LD_LIBRARY_PATH'] = str(private)
                        actual, attempts = generator.generate_map(Path(bundle['directory']) / bundle['executable'], slot['spec'], out, env)
                        record = dict(group=slot['group'], planned_spec_sha256=digest(slot['spec']), spec=actual,
                                      path=str(out / 'map.map.gz'), sha256=file_hash(out / 'map.map.gz'),
                                      generation_attempts=attempts, mapgen_bundle=bundle['id'])
                        atomic_json(saved, record)
                    records.append(record)
            finally:
                generator.ROOT = old_root
        for r in records:
            if file_hash(r['path']) != r['sha256']:
                raise ValueError('Accepted map bytes changed')
        routing = tournament.accepted_routing(document, records)
        if any(map_identity(r['spec']) not in read(registry)['assignments'] for r in records):
            raise ValueError('Accepted map was outside predeclared split identities')
        atomic_json(directory / 'accepted-maps.json', records)
        atomic_json(directory / 'actual-routing.json', routing)
        result = dict(plan=str(directory / 'plan.json'), maps=str(directory / 'accepted-maps.json'),
                      routing=str(directory / 'actual-routing.json'), registry=str(registry))
        return result, [*result.values(), *[r['path'] for r in records]]

    def collect(self, directory, design, bundles, role, seed):
        args = SimpleNamespace(plan=design['plan'], maps=design['maps'], split_registry=design['registry'],
                               routing_amendment=design['routing'], bundle=bundles, role=role,
                               cohort=digest(dict(campaign=self.state['identity'], directory=str(directory), role=role))[:24],
                               seed=seed, intervals=self.config.get('intervals') if role == 'exploration' else None,
                               output=str(directory / 'collection'))
        tournament.prepare(args)
        inspected = [inspect_bundle(p) for p in bundles]
        hosts = copy.deepcopy(self.hosts)
        for host in hosts:
            host['builds'] = [b['id'] for b in inspected if b['platform'] == host['platform']
                              and b['build_options'].get('execution_pool') in self.config['host_pools'][host['name']]]
            if not host['builds']:
                raise ValueError('No compatible pool bundle for host: ' + host['name'])
        atomic_json(directory / 'hosts.json', hosts)
        c = Coordinator(directory / 'collection')
        try:
            admitted = {b for host in hosts for b in host['builds']}
            if {j['build'] for j in c.manifest['jobs']} - admitted:
                raise ValueError('At least one frozen job has no configured compatible host')
            status = c.run(hosts)
        finally:
            c.close()
        if any(status['jobs'].get(k, 0) for k in ('pending', 'active')):
            raise ValueError('Collection stopped before terminal; resume existing coordinator, never replace slots')
        accounting = exporter.export(directory / 'collection', directory / 'export')
        manifests = terminal_accounting(accounting)
        spatial.load(manifests)
        return manifests, [directory / 'hosts.json', directory / 'collection/experiment.json',
                           directory / 'export/terminal-accounting.json', *cohort_files(manifests)]

    def fit_emit(self, directory, manifests, registry):
        directory.mkdir(parents=True, exist_ok=True)
        spatial.load(manifests)  # Strict contracts, episode hashes and identities.
        research = directory / 'research.json'
        request = dict(action='fit-unified-generic12', contract=spatial.CONTRACT,
                       manifests=checked_files(manifests), split_registry=str(registry),
                       split_registry_sha256=file_hash(registry), expected_families=self.config['expected_families'],
                       output=str(research), research_only=True)
        if 'fit_hook' in self.config:
            receipt = self.hook('fit_hook', request, directory / 'fit-hook')
            if receipt.get('status') != 'fit-complete' or receipt.get('model_sha256') != file_hash(research):
                raise ValueError('Fit adapter must return the exact research model locally')
        else:
            model.fit(SimpleNamespace(manifest=manifests, split_registry=str(registry), output=str(research),
                                      expected_families=','.join(self.config['expected_families']),
                                      collector_contract=spatial.CONTRACT_ID))
        fitted = read(research)
        provenance = fitted.get('provenance', {})
        if (fitted.get('candidate_contract') != data.CONTRACT
                or provenance.get('expected_families') != self.config['expected_families']
                or {m['path']: m['sha256'] for m in provenance.get('manifests', [])} != request['manifests']
                or provenance.get('split', {}).get('registry', {}).get('sha256') != file_hash(registry)):
            raise ValueError('Research fit provenance differs from requested data/splits/family scope')
        emitted, header = directory / 'candidate.json', directory / 'AdaptiveGradientModel.h'
        model.emit(SimpleNamespace(model=str(research), contract=data.CONTRACT_ID, output=str(emitted), header=str(header)))
        return dict(research=str(research), model=str(emitted), header=str(header)), [research, emitted, header]

    def build(self, directory, fit):
        request = dict(action='ordinary-build-and-qualify-research', source_model=fit['model'],
                       source_header=fit['header'], model_sha256=file_hash(fit['model']),
                       header_sha256=file_hash(fit['header']), collector_contract=spatial.CONTRACT,
                       collector_contract_sha256=data.canonical_hash(spatial.CONTRACT), stable_promotion=False)
        receipt = self.hook('build_hook', request, directory)
        bundles = qualification(receipt, fit['model'], fit['header'])
        return bundles, [directory / 'receipt.json', *[Path(p) / 'bundle.json' for p in bundles]]

    def run(self):
        manifests = [str(self.path(p)) for p in self.config.get('prior_manifests', [])]
        if manifests: spatial.load(manifests)
        previous_registry = self.path(self.config['split_registry'])
        bundles = self.bundle_paths
        for number, spec in enumerate(self.config['rounds'], 1):
            out = self.root / f'round-{number:03d}'
            prefix = out.name + '/'
            design = self.stage(prefix + 'design', lambda: self.design(out, spec, previous_registry))
            exploration = self.stage(prefix + 'exploration', lambda: self.collect(out / 'exploration', design, bundles, 'exploration', spec['seed']))
            manifests += exploration
            fit = self.stage(prefix + 'candidate-fit', lambda: self.fit_emit(out / 'candidate-fit', manifests, design['registry']))
            bundles = self.stage(prefix + 'candidate-build', lambda: self.build(out / 'candidate-build', fit))
            # Revalidate actual bundle payloads on resume, not just their manifests.
            qualification(read(out / 'candidate-build/receipt.json'), fit['model'], fit['header'])
            candidate = self.stage(prefix + 'candidate', lambda: self.collect(out / 'candidate', design, bundles, 'candidate', spec['seed']))
            manifests += candidate
            final = self.stage(prefix + 'cumulative-fit', lambda: self.fit_emit(out / 'cumulative-fit', manifests, design['registry']))
            if self.config.get('spatial_analysis'):
                def compare():
                    path = out / 'spatial-research.json'
                    spatial.compare(SimpleNamespace(manifest=manifests, split_registry=design['registry'], output=str(path)))
                    return str(path), [path]
                self.stage(prefix + 'spatial-research', compare)
            previous_registry = Path(design['registry'])
        self.state['result'] = dict(status='complete-research-only', final=final, manifests=manifests,
                                    split_registry=str(previous_registry), stable_promotion=False)
        atomic_json(self.state_path, self.state)
        return self.state['result']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=['run', 'status'])
    p.add_argument('--config')
    p.add_argument('--directory', required=True)
    a = p.parse_args()
    if a.command == 'status':
        print(json.dumps(read(Path(a.directory) / 'campaign.json'), indent=2))
        return
    if not a.config: p.error('run requires --config')
    root = Path(a.directory).resolve()
    root.mkdir(parents=True, exist_ok=True)
    with lock(root / 'campaign.lock', blocking=False):
        print(json.dumps(Campaign(a.config, root).run(), indent=2))


if __name__ == '__main__':
    main()
