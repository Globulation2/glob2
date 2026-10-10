"""An explicit free cohort cannot borrow evidence or bytes from other channels."""
import copy
import io
import json
from pathlib import Path
import sys
import tarfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_downloads_manifest as fixtures
MODULE, STAGE = fixtures.MODULE, fixtures.STAGE
from staged_bundle import extract

BUILD = dict(role='client', client_profile='free', mode='release', target='native', distribution='direct',
             client_features=dict(commander=False, authoring_links=False, community_ai=True, community_generators=True))

class FreeCohortTests(unittest.TestCase):
    # Reuse fixture setup, not all-platform test methods.
    def setUp(self):
        fixtures.ManifestTests.setUp(self)
        self.inventory['schemaVersion'] = 2
        self.inventory['packages'] = [p for p in self.inventory['packages'] if p['platform'] == 'macos' and p['architecture'] == 'arm64']
        self.inventory['packages'][0]['clientBuild'] = copy.deepcopy(BUILD)
        self.evidence['schemaVersion'] = 2
        self.evidence['stores'] = {}
        self.evidence['platforms'] = {'macos': self.evidence['platforms']['macos']}
        self.evidence['testedTargets'] = {'macos-arm64': True}
        self.evidence['artifacts'] = {p['filename']: self.evidence['artifacts'][p['filename']] for p in self.inventory['packages'] + self.inventory['sources']}
        self.evidence['clientBuilds'] = {p['filename']: p['clientBuild'] for p in self.inventory['packages']}

    def generate(self):
        return MODULE.generate(self.directory, self.inventory, self.evidence, self.tag, self.sha, staged=True)

    def test_selected_channel_does_not_wait_for_unselected_stores(self):
        manifest = self.generate()
        self.assertEqual(manifest['schemaVersion'], 2)
        self.assertEqual(manifest['qualification']['stores'], {})
        self.assertEqual(manifest['qualification']['testedTargets'], ['macos-arm64'])
        self.assertEqual(len(manifest['packages']), 1)
        with self.assertRaises(ValueError):
            MODULE.generate(self.directory, self.inventory, self.evidence, self.tag, self.sha)

    def test_exact_build_identity_attestation(self):
        self.evidence['clientBuilds'] = {}
        with self.assertRaisesRegex(ValueError, 'clientBuilds'): self.generate()

    def test_free_profile_and_final_release_flags_required(self):
        original = copy.deepcopy(BUILD)
        for key, value in [('client_profile', 'full'), ('mode', 'debug'), ('dev_fast', True), ('unity', True), ('pch', True)]:
            self.inventory['packages'][0]['clientBuild'] = dict(original, **{key: value})
            with self.subTest(key=key), self.assertRaises(ValueError): self.generate()
        for key in ('commander', 'authoring_links'):
            invalid = copy.deepcopy(original); invalid['client_features'][key] = True
            self.inventory['packages'][0]['clientBuild'] = invalid
            with self.subTest(key=key), self.assertRaises(ValueError): self.generate()

    def test_selected_platform_still_needs_qualification(self):
        self.evidence['platforms']['macos']['notarized'] = False
        with self.assertRaisesRegex(ValueError, 'notarized'): self.generate()

    def test_empty_selection_rejected(self):
        self.inventory['packages'] = []
        with self.assertRaisesRegex(ValueError, 'at least one playable'): self.generate()

    def test_stage_does_not_require_unselected_outputs(self):
        filename='Glob2-1.2.3-macos-arm64.dmg'
        (self.directory/filename).write_bytes(b'final')
        result = STAGE.inventory(self.directory,self.tag,self.sha,{('macos','arm64','dmg')},{'macos:arm64:dmg': BUILD})
        self.assertEqual(len(result['packages']),1)
        self.assertEqual(result['packages'][0]['filename'],filename)
        self.assertEqual(result['schemaVersion'],2)
        with self.assertRaises(ValueError): STAGE.inventory(self.directory,self.tag,self.sha,set(),{})

    def test_safe_bundle_extraction(self):
        bundle=self.directory/'cohort.tar.gz'
        def archive(extra=None):
            with tarfile.open(bundle,'w:gz') as output:
                data={'package-inventory.json': json.dumps(self.inventory).encode()}
                data.update({p['filename']: (self.directory/p['filename']).read_bytes() for p in self.inventory['packages']+self.inventory['sources']})
                for name, payload in data.items():
                    info=tarfile.TarInfo(name);info.size=len(payload);output.addfile(info,io.BytesIO(payload))
                if extra:
                    info=tarfile.TarInfo(extra);info.size=1;output.addfile(info,io.BytesIO(b'x'))
        archive(); extract(bundle,self.directory/'safe')
        self.assertTrue((self.directory/'safe/package-inventory.json').is_file())
        for name in ('../escape', '/absolute', 'unlisted.exe', 'package-inventory.json'):
            archive(name)
            with self.subTest(name=name), self.assertRaises(ValueError): extract(bundle,self.directory/'bad')

if __name__ == "__main__": unittest.main()
