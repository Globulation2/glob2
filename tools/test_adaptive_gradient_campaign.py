# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import json
from pathlib import Path
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
import unittest
from unittest.mock import patch

from tools import adaptive_gradient_campaign as c
from tools.resource_gradient_dataset import seal
from tools.test_resource_gradient_tournament import catalog
from tools.tournaments.bundles import register_bundle
from tools.tournaments.common import atomic_json, file_hash, lock


class CampaignTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.write('catalog.json', catalog())
        self.write('splits.json', dict(schema=1, strategy='map-hash-713-v1', assignments={'historical-heldout': -1}))
        self.write('hosts.json', [dict(name='local', transport='local', directory=str(self.root / 'worker'),
                                       platform=dict(os='linux', arch='x86_64'), slots=1)])
        self.write('maps.json', [])
        (self.root / 'hook.py').write_text('# trusted test adapter\n')
        source = self.root / 'runtime'
        source.mkdir()
        (source / 'glob2').write_text('fixture executable, never run')
        (source / 'glob2').chmod(0o755)
        bundle = register_bundle(source, self.root / 'bundles', 'glob2', 'frozen-source',
                                 options=dict(release=1, optimization='O3', dev_fast=0, pch=0, unity=0,
                                              model_sha256='1'*64, header_sha256='2'*64,
                                              model_contract=c.data.CONTRACT_ID, collector_contract=c.spatial.CONTRACT_ID,
                                              adaptive_gradient_contract_sha256=c.data.canonical_hash(c.spatial.CONTRACT),
                                              unified_model_trained=False),
                                 capabilities=dict(schema_version=1, commands=['game']),
                                 target_platform=dict(os='linux', arch='x86_64'))
        self.bundle = self.root / 'bundles' / bundle['id']
        self.config = dict(schema=1, catalog='catalog.json', bootstrap_bundles=[str(self.bundle)],
                           hosts='hosts.json', host_pools={'local': ['small-pharaoh', 'middle-linux']},
                           split_registry='splits.json', expected_families=['materials', 'guard', 'clear', 'forbidden'],
                           routing='remote-only', rounds=[dict(seed=181101, accepted_maps='maps.json')],
                           build_hook=dict(argv=[sys.executable, str(self.root / 'hook.py'), '{request}', '{receipt}'], inputs=['hook.py']))
        self.write('config.json', self.config)
        self.identity = patch.object(c, 'tool_identity', return_value={'fixed-test-tools': 1})
        self.identity.start()
        self.addCleanup(self.identity.stop)

    def write(self, name, value):
        path = self.root / name
        atomic_json(path, value)
        return path

    def campaign(self):
        return c.Campaign(self.root / 'config.json', self.root / 'campaign')

    def test_real_design_preserves_historical_splits_and_predeclares_fallbacks(self):
        plan = c.tournament.plan(catalog(), 181101, 'remote-only')
        mapfile = self.root / 'map.map.gz'
        mapfile.write_bytes(b'immutable already-generated fixture')
        maps = [dict(group=r['group'], spec={k: v for k, v in r['spec'].items() if k != 'map_candidates'},
                     path=str(mapfile), sha256=file_hash(mapfile)) for r in plan['unified']['slots']]
        self.write('maps.json', maps)
        campaign = self.campaign()
        result, outputs = campaign.design(campaign.root / 'round-001', self.config['rounds'][0], self.root / 'splits.json')
        registry = c.read(result['registry'])['assignments']
        self.assertEqual(registry['historical-heldout'], -1)
        self.assertTrue(all(c.map_identity(r['spec']) in registry for r in maps))
        self.assertEqual(len(c.read(result['maps'])), 256)
        self.assertEqual(c.read(result['routing'])['phase'], 'post-generation-before-any-simulation-outcomes')
        self.assertIn(str(mapfile.resolve()), map(str, outputs))
        self.assertEqual(c.read(self.root / 'splits.json')['assignments'], {'historical-heldout': -1})
        # Simulate interruption before the design stage ledger was committed.
        original = c.read(result['registry'])
        new_group = next(k for k in original['assignments'] if k != 'historical-heldout')
        for group in ['historical-heldout', new_group]:
            altered = copy.deepcopy(original)
            altered['assignments'][group] = 0 if original['assignments'][group] != 0 else 1
            atomic_json(result['registry'], altered)
            with self.assertRaisesRegex(ValueError, 'Provisional map split'):
                campaign.design(campaign.root / 'round-001', self.config['rounds'][0], self.root / 'splits.json')
            self.assertEqual(c.read(result['registry']), altered)  # Do not hide the corruption.
            atomic_json(result['registry'], original)

    def test_pipeline_cumulative_fit_order_and_resume_never_redispatch(self):
        campaign = self.campaign()
        calls = []
        def design(directory, spec, registry):
            directory.mkdir(parents=True)
            p = directory / 'map-splits.json'
            atomic_json(p, c.read(registry))
            return dict(registry=str(p)), [p]
        def collect(directory, design, bundles, role, seed):
            directory.mkdir(parents=True)
            p = directory / 'cohort.json'
            atomic_json(p, dict(role=role))
            calls.append(role)
            return [str(p)], [p]
        def fit(directory, manifests, registry):
            directory.mkdir(parents=True)
            calls.append(('fit', len(manifests)))
            paths = {k: str(directory / (k + '.json')) for k in ['research', 'model', 'header']}
            for p in paths.values(): atomic_json(p, dict(manifests=manifests))
            return paths, list(paths.values())
        def build(directory, fit):
            directory.mkdir(parents=True)
            atomic_json(directory / 'receipt.json', {})
            calls.append('build')
            return [str(self.bundle)], [directory / 'receipt.json']
        with patch.object(c.Campaign, 'design', side_effect=design), patch.object(c.Campaign, 'collect', side_effect=collect), \
             patch.object(c.Campaign, 'fit_emit', side_effect=fit), patch.object(c.Campaign, 'build', side_effect=build), \
             patch.object(c, 'qualification', return_value=[str(self.bundle)]):
            first = campaign.run()
            self.assertEqual(calls, ['exploration', ('fit', 1), 'build', 'candidate', ('fit', 2)])
            self.assertFalse(first['stable_promotion'])
            self.assertEqual(self.campaign().run(), first)
            self.assertEqual(len(calls), 5)
            Path(first['manifests'][0]).write_text('changed')
            with self.assertRaisesRegex(ValueError, 'Frozen stage'):
                self.campaign().run()

    def test_config_and_hook_code_cannot_change_on_resume(self):
        self.campaign()
        (self.root / 'hook.py').write_text('# changed\n')
        with self.assertRaisesRegex(ValueError, 'frozen inputs changed'):
            self.campaign()

    def test_ordinary_host_file_without_platform_rejected_before_any_jobs(self):
        hosts = c.read(self.root / 'hosts.json')
        hosts[0].pop('platform')  # Ordinary coordinator files use discovery.
        self.write('hosts.json', hosts)
        with patch.object(c.tournament, 'prepare') as prepare, patch.object(c, 'Coordinator') as coordinator:
            with self.assertRaisesRegex(ValueError, 'explicit platform'):
                self.campaign()
            prepare.assert_not_called()
            coordinator.assert_not_called()
        self.assertFalse((self.root / 'campaign/campaign.json').exists())

    def test_bootstrap_release_contract_and_common_identity_fail_closed(self):
        original = c.inspect_bundle(self.bundle)
        c.validate_bundle_set([original])  # Explicit untrained positive-interval bootstrap is valid.
        for field, value in [('release', 0), ('dev_fast', 1), ('pch', 1), ('unity', 1),
                             ('optimization', 'O0'), ('model_contract', 'resource-only'),
                             ('adaptive_gradient_contract_sha256', 'wrong')]:
            bad = copy.deepcopy(original)
            bad['build_options'][field] = value
            with patch.object(c, 'inspect_bundle', return_value=bad), patch.object(c.tournament, 'prepare') as prepare:
                with self.assertRaisesRegex(ValueError, 'ordinary O3'):
                    self.campaign()
                prepare.assert_not_called()
        second = copy.deepcopy(original)
        second['build_options']['header_sha256'] = '3'*64
        with self.assertRaisesRegex(ValueError, 'must agree'):
            c.validate_bundle_set([original, second])

    def test_failed_qualification_never_dispatches_reserved_candidate(self):
        campaign = self.campaign()
        design = {'registry': str(self.root / 'splits.json')}
        fit = {'model': 'staged-model', 'header': 'staged-header'}
        with patch.object(campaign, 'design', return_value=(design, [])), \
             patch.object(campaign, 'collect', return_value=([], [])) as collect, \
             patch.object(campaign, 'fit_emit', return_value=(fit, [])), \
             patch.object(campaign, 'build', side_effect=ValueError('unqualified')):
            with self.assertRaisesRegex(ValueError, 'unqualified'):
                campaign.run()
            self.assertEqual(collect.call_count, 1)
            self.assertEqual(collect.call_args.args[3], 'exploration')
            self.assertNotIn('round-001/candidate', campaign.state['stages'])

    def test_explicit_family_scope_and_argv_only(self):
        self.config['expected_families'] = ['materials', 'made-up']
        self.write('config.json', self.config)
        with self.assertRaisesRegex(ValueError, 'family'):
            self.campaign()
        self.config['expected_families'] = ['materials']
        self.config['build_hook']['argv'] = 'unsafe shell string'
        self.write('config.json', self.config)
        with self.assertRaisesRegex(ValueError, 'argv'):
            self.campaign()

    def test_caps_are_terminal_but_crashes_cancelled_and_missing_slots_block(self):
        value = dict(expected=128, counts={'completed': 53, 'user-censored': 75}, completed_manifests=['a'], censored_manifests=['b'])
        self.assertEqual(c.terminal_accounting(value), ['a', 'b'])
        for counts in [{'completed': 127}, {'completed': 127, 'other-failure': 1}, {'completed': 127, 'unresolved': 1}]:
            with self.assertRaisesRegex(ValueError, 'all128'):
                c.terminal_accounting(dict(value, counts=counts))

    def test_sealed_episode_tampering_is_checked_not_just_manifest_bytes(self):
        episode = self.root / 'episodes.csv'
        episode.write_text('complete\n1\n')
        raw = self.write('raw.json', [dict(episodes=str(episode), episode_sha256=file_hash(episode))])
        cohort = dict(id='cohort', policy={'name': 'test'}, contract=c.spatial.CONTRACT)
        target = self.root / 'sealed.json'
        seal(raw, target, cohort)
        self.assertIn(episode, c.cohort_files([str(target)]))
        episode.write_text('complete\n0\n')
        with self.assertRaisesRegex(ValueError, 'episode bytes'):
            c.cohort_files([str(target)])

    def test_hook_is_synchronous_and_receipt_bound_to_request(self):
        script = self.root / 'hook.py'
        script.write_text('import sys,json,hashlib,pathlib\np=pathlib.Path(sys.argv[1]);pathlib.Path(sys.argv[2]).write_text(json.dumps(dict(request_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),status="done")))\n')
        campaign = self.campaign()
        out = self.root / 'adapter'
        self.assertEqual(campaign.hook('build_hook', {'exact': 1}, out)['status'], 'done')
        with self.assertRaisesRegex(ValueError, 'frozen request'):
            campaign.hook('build_hook', {'exact': 2}, out)

    def test_receipt_then_failed_exit_never_becomes_success_on_resume(self):
        (self.root / 'hook.py').write_text('import sys,json,hashlib,pathlib\np=pathlib.Path(sys.argv[1]);pathlib.Path(sys.argv[2]).write_text(json.dumps(dict(request_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),status="qualified-research")));sys.exit(1)\n')
        campaign = self.campaign()
        out = self.root / 'failed-adapter'
        with self.assertRaisesRegex(ValueError, 'exit1'):
            campaign.hook('build_hook', {'exact': 1}, out)
        self.assertFalse((out / 'success.json').exists())
        with self.assertRaisesRegex(ValueError, 'completion is ambiguous'):
            campaign.hook('build_hook', {'exact': 1}, out)

    def test_early_receipt_while_child_is_live_cannot_be_reused(self):
        (self.root / 'hook.py').write_text('import sys,json,hashlib,pathlib,time\np=pathlib.Path(sys.argv[1]);r=pathlib.Path(sys.argv[2]);r.write_text(json.dumps(dict(request_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),status="done")))\nwhile not (r.parent/"release").exists():time.sleep(0.01)\n')
        campaign = self.campaign()
        out = self.root / 'live-adapter'
        with ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(campaign.hook, 'build_hook', {'exact': 1}, out)
            try:
                deadline = time.monotonic() + 5
                while not (out / 'receipt.json').exists() and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertTrue((out / 'receipt.json').exists())
                self.assertFalse(future.done())
                with self.assertRaisesRegex(ValueError, 'completion is ambiguous'):
                    campaign.hook('build_hook', {'exact': 1}, out)
            finally:
                (out / 'release').touch()
            self.assertEqual(future.result(timeout=5)['status'], 'done')
        self.assertEqual(campaign.hook('build_hook', {'exact': 1}, out)['status'], 'done')

    def test_launch_intent_precedes_spawn_and_unknown_launch_blocks_resume(self):
        campaign = self.campaign()
        out = self.root / 'interrupted-launch'
        def interrupted(*args, **kwargs):
            self.assertEqual(c.read(out / 'running.json')['state'], 'launch-intent')
            raise OSError('interrupted around spawn')
        with patch.object(c.subprocess, 'Popen', side_effect=interrupted):
            with self.assertRaisesRegex(OSError, 'interrupted'):
                campaign.hook('build_hook', {'exact': 1}, out)
        with patch.object(c.subprocess, 'Popen') as spawn:
            with self.assertRaisesRegex(ValueError, 'completion is ambiguous'):
                campaign.hook('build_hook', {'exact': 1}, out)
            spawn.assert_not_called()

    def test_single_driver_lock_prevents_duplicate_dispatch(self):
        with lock(self.root / 'lock', blocking=False):
            with self.assertRaises((BlockingIOError, OSError)):
                with lock(self.root / 'lock', blocking=False):
                    self.fail('duplicate driver acquired lock')


class QualificationTests(unittest.TestCase):
    def test_receipt_requires_exact_coefficients_and_native_checks(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp); m = p / 'model'; h = p / 'header'
            m.write_text('model'); h.write_text('header')
            receipt = dict(status='qualified-research', stable_promotion=False,
                           model_sha256=file_hash(m), header_sha256=file_hash(h),
                           collector_contract_sha256=c.data.canonical_hash(c.spatial.CONTRACT),
                           checks=dict(integer_parity=True, determinism_smoke=True, simulation_gates=True), bundles=['frozen'])
            options = dict(model_sha256=file_hash(m), header_sha256=file_hash(h),
                           adaptive_gradient_contract_sha256=receipt['collector_contract_sha256'],
                           model_contract=c.data.CONTRACT_ID, collector_contract=c.spatial.CONTRACT_ID,
                           unified_model_trained=True, release=1, optimization='O3', dev_fast=0, pch=0, unity=0)
            bundle = dict(source_revision='source', platform={'os': 'linux', 'arch': 'x86_64'},
                          executable='glob2', files=[{'path': 'glob2', 'sha256': 'elf'}], build_options=options)
            receipt['binaries'] = [dict(binary_sha256='elf', source_revision='source', platform=bundle['platform'])]
            with patch.object(c, 'inspect_bundle', return_value=bundle):
                self.assertEqual(c.qualification(receipt, m, h), ['frozen'])
                for field, value in [('model_sha256', 'wrong'), ('stable_promotion', True), ('binaries', [])]:
                    with self.assertRaises(ValueError): c.qualification(dict(receipt, **{field: value}), m, h)
                bad = copy.deepcopy(receipt); bad['checks']['determinism_smoke'] = False
                with self.assertRaisesRegex(ValueError, 'determinism'):
                    c.qualification(bad, m, h)


if __name__ == '__main__':
    unittest.main()
