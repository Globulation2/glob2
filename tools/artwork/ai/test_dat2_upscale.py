"""DAT2 staging must reproduce its recipe, never write the pack, and promote only what was reviewed."""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from PIL import Image

from tools.artwork.ai import dat2_upscale as dat2

try:
    import cv2  # noqa: F401  (assembly needs OpenCV from requirements-dat2.txt)
    HAVE_CV2 = True
except ImportError:
    HAVE_CV2 = False


class FakeAdapter:
    """Nearest-neighbour '4x model' with the spandrel adapter interface."""

    def __init__(self, fail_on=None):
        self.name, self.calls, self.fail_on = None, 0, fail_on
        self.scale, self.minimum, self.multiple_of = 4, 16, 1

    def use(self, name):
        self.name = name

    def forward(self, patch):
        self.calls += 1
        if self.fail_on is not None and self.calls >= self.fail_on:
            raise RuntimeError('fake inference failure')
        return np.repeat(np.repeat(patch, 4, 0), 4, 1).clip(0, 1)

    def release(self):
        pass


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sprite(size, alpha, colour, team=False):
    w, h = size
    pixels = np.zeros((h, w, 4), np.uint8)
    yy, xx = np.mgrid[:h, :w]
    inside = (abs(yy - h / 2) < h / 3) & (abs(xx - w / 2) < w / 3)
    pixels[inside] = colour + (alpha,)
    if not team:
        pixels[inside & (xx % 3 == 0)] = (40, 30, 20, alpha)  # some structure
    return pixels


class Repo:
    """A minimal, self-consistent approved pack in a temporary directory."""

    FRAMES = {  # id: (recipe, has team)
        'inn0b0': ('outline_repair; colour sharpening v1', True),
        'inn0c0': ('painted_repair; colour sharpening v1', False),
        'school1b0': ('recovered original building: selected GIMP base/team groups; saved opacity and native alpha',
                      True),
        'wall0': ('current; colour sharpening v1', True),
        'ressource0': ('resource constrained; crisp edges and sharpening v1', False),
    }

    def __init__(self, root):
        self.root = root
        self.gfx = root / 'content/data/gfx'
        self.production = root / dat2.PRODUCTION
        self.runtime = root / dat2.RUNTIME
        for d in (self.gfx, self.production / 'pack-metadata', self.runtime, root / 'content/data/buildings',
                  root / 'tools/artwork/ai'):
            d.mkdir(parents=True, exist_ok=True)
        (root / 'tools/artwork/ai/dat2_upscale.py').write_bytes(Path(dat2.__file__).read_bytes())
        (root / 'content/data/buildings/inn.json').write_text(json.dumps(
            {'levels': [{'gameSprite': 'data/gfx/inn0b'}, {'constructionSprite': {'gameSprite': 'data/gfx/inn0c'}}]}))
        (root / 'content/data/buildings/other.json').write_text(json.dumps(
            [{'gameSprite': 'data/gfx/school1b'}, {'gameSprite': 'data/gfx/wall'}]))
        frames, rows, files = [], [], []
        for frame_id, (recipe, has_team) in self.FRAMES.items():
            size = (20, 18)
            layers = []
            for role, suffix in (('base', '.png'), ('team', 'r.png')) if has_team else (('base', '.png'),):
                name = frame_id + suffix
                Image.fromarray(sprite(size, 200 if role == 'base' else 150,
                                       (0, 190, 0) if role == 'team' else (150, 120, 90), role == 'team'),
                                'RGBA').save(self.gfx / name)
                hd = Image.new('RGBA', (size[0] * 4, size[1] * 4), (10, 20, 30, 255))
                folder = package_category(recipe)
                (self.production / folder).mkdir(exist_ok=True)
                hd.save(self.production / folder / name)
                (self.runtime / name).write_bytes((self.production / folder / name).read_bytes())
                digest = sha(self.runtime / name)
                layers.append(dict(file=name, role=role, sha256=digest, source_sha256=digest,
                                   logical_width=size[0], logical_height=size[1],
                                   original_sha256=sha(self.gfx / name)))
                files.append(dict(runtime=name, source=f'{folder}/{name}', sha256=digest))
            frames.append(dict(id=frame_id, width=size[0], height=size[1], scale=4, recipe=recipe,
                               layers=layers, sources=[]))
            rows.append(f'{frame_id} {size[0]} {size[1]} 4 {frame_id}.png '
                        + (f'{frame_id}r.png' if has_team else '-'))
        meta = self.production / 'pack-metadata'
        (meta / 'manifest.json').write_text(json.dumps(dict(version=1, frames=frames), indent=2) + '\n')
        (meta / 'frames.txt').write_text('GLOB2_HIGHRES 1\n' + '\n'.join(rows) + '\n')
        (meta / 'README.md').write_text('pack\n')
        for name in ('manifest.json', 'frames.txt', 'README.md'):
            (self.runtime / name).write_bytes((meta / name).read_bytes())
            files.append(dict(runtime=name, source=f'pack-metadata/{name}', sha256=sha(meta / name)))
        (self.production / 'package.json').write_text(json.dumps(dict(version=1, files=files), indent=2) + '\n')

    def snapshot(self):
        return {str(p.relative_to(self.root)): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}


def package_category(recipe):
    from tools.artwork.package_runtime import category
    return category(recipe)



class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Repo(Path(self.temp.name) / 'repo')

    def test_building_set_excludes_originals_walls_and_non_buildings(self):
        self.assertEqual(dat2.all_building_frames(self.repo.root), ['inn0b0', 'inn0c0'])
        for frame_id in ('school1b0', 'wall0', 'ressource0x', 'bad/id'):
            with self.assertRaises(ValueError):
                dat2.validate_frame(self.repo.root, frame_id)

    def test_rejects_missing_classic_team_layer(self):
        (self.repo.gfx / 'inn0b0r.png').unlink()
        with self.assertRaises(ValueError):
            dat2.validate_frame(self.repo.root, 'inn0b0')

    def test_staging_must_be_fresh_and_outside_the_repo(self):
        for output in (self.repo.root, self.repo.root / 'content/stage', self.repo.root / 'artifacts'):
            with self.assertRaises(ValueError):
                dat2.staging_output(self.repo.root, output)
        existing = Path(self.temp.name) / 'existing'
        existing.mkdir()
        with self.assertRaises(ValueError):
            dat2.staging_output(self.repo.root, existing)
        allowed = self.repo.root / 'artifacts/dat2'
        self.assertEqual(dat2.staging_output(self.repo.root, allowed), allowed.resolve())

    def test_only_commercially_licensed_variants_are_classified(self):
        from tools.artwork import package_runtime, runtime_provenance
        self.assertFalse(dat2.promotable('usv2-bhi'))
        for variant in ('bhi', 'bhi-nomos', 'bhi-atd'):
            self.assertTrue(dat2.promotable(variant))
        classified = dat2.recipe_names() & package_runtime.AI
        self.assertTrue(classified)
        self.assertLessEqual(classified, dat2.recipe_names(promotable_only=True))  # never a reference-only one
        for name in classified:
            self.assertEqual(package_runtime.category(name), 'ai-upscaled')
            self.assertIn(name, runtime_provenance.CATEGORIES)

    def test_variant_recipes_differ_only_in_colour_sources(self):
        for variant, info in dat2.VARIANTS.items():
            recipe = dat2.variant_recipe(variant)
            self.assertEqual(recipe['sharp'], tuple(m + '/chain' for m in info['models']))
            self.assertEqual(recipe['alpha'], dat2.alpha_model_of(variant) + '/chain')
            self.assertIn(dat2.alpha_model_of(variant), info['models'])
            self.assertEqual({k: v for k, v in recipe.items() if k not in ('sharp', 'smooth', 'alpha')},
                             {k: v for k, v in dat2.RECIPE.items() if k not in ('sharp', 'smooth', 'alpha')})

    @unittest.skipUnless(HAVE_CV2, 'needs OpenCV (requirements-dat2.txt)')
    def test_three_model_gate_uses_the_largest_pairwise_disagreement(self):
        a, b = np.full((8, 8, 3), 0.5, np.float32), np.full((8, 8, 3), 0.5, np.float32)
        far = np.full((8, 8, 3), 0.9, np.float32)
        smooth = [np.zeros((8, 8, 3), np.float32)] * 3
        agree = (0.05, 0.15, 0.8)
        self.assertTrue(np.allclose(dat2.mix_sources([a, b, b], smooth, agree), 0.5))
        # one dissenting model drives the fallback as far as a two-model disagreement would
        self.assertTrue(np.allclose(dat2.mix_sources([a, b, far], smooth, agree),
                                    (0.5 + 0.5 + 0.9) / 3 * (1 - 0.8)))

    def test_weights_parse_validate_and_drop_zero_models(self):
        spec = dat2.weighted_spec('bhi=0.5,nomos=0.3,atd=0.2')
        self.assertEqual(spec['name'], dat2.WEIGHTED_NAME)
        self.assertAlmostEqual(sum(spec['weights'].values()), 1)
        self.assertEqual(spec['alpha'], '4xBHI_dat2_real')
        self.assertTrue(dat2.promotable(spec))
        self.assertEqual(dat2.variant_recipe(spec)['weights'], (0.5, 0.3, 0.2))
        for bad in ('bhi=0.5,nomos=0.3', 'bhi=1.2,atd=-0.2', 'foo=1', 'bhi=0.5,4xBHI_dat2_real=0.5', 'bhi'):
            with self.assertRaises(ValueError):
                dat2.weighted_spec(bad)
        zero = dat2.weighted_spec({'bhi': 0, 'nomos': 0.25, 'atd': 0.75})
        self.assertEqual(zero['models'], ('4xNomos8kDAT', '4xRealWebPhoto_v3_atd'))
        self.assertEqual(zero['alpha'], '4xRealWebPhoto_v3_atd')  # heaviest model without BHI
        self.assertFalse(dat2.promotable(dat2.weighted_spec('usv2=0.5,bhi=0.5')))

    @unittest.skipUnless(HAVE_CV2, 'needs OpenCV (requirements-dat2.txt)')
    def test_weighted_mix_equal_weights_and_zero_weights(self):
        rng = np.random.default_rng(3)
        a, b, c = (rng.random((12, 12, 3)).astype(np.float32) for _ in range(3))
        ta, tb, tc = (rng.random((12, 12, 3)).astype(np.float32) for _ in range(3))
        flat = rng.random((12, 12)).astype(np.float32)
        agree = (0.05, 0.15, 0.8)
        plain = dat2.mix_sources([a, b, c], [ta, tb, tc], agree, flat)
        equal = dat2.mix_sources([a, b, c], [ta, tb, tc], agree, flat, (1 / 3, 1 / 3, 1 / 3))
        self.assertTrue(np.array_equal(plain, equal))  # bit-exact: the existing ensemble
        weighted = dat2.mix_sources([a, b], [ta, tb], (0.05, 0.15, 0.0), None, (0.75, 0.25))
        self.assertTrue(np.allclose(weighted, 0.75 * a + 0.25 * b, atol=1e-6))

    def test_single_model_mix_uses_only_the_flat_fallback(self):
        sharp = [np.full((4, 4, 3), 0.8, np.float32)]
        smooth = [np.full((4, 4, 3), 0.2, np.float32)]
        flat = np.full((4, 4), 0.5, np.float32)
        self.assertTrue(np.allclose(dat2.mix_sources(sharp, smooth, (0.05, 0.15, 0.8)), 0.8))
        self.assertTrue(np.allclose(dat2.mix_sources(sharp, smooth, (0.05, 0.15, 0.8), flat), 0.5))


class InferenceTests(unittest.TestCase):
    def test_tiled_inference_blends_tiles_exactly(self):
        rng = np.random.default_rng(1)
        image = rng.integers(0, 256, (20, dat2.TILE + 90, 3), dtype=np.uint8)
        out = dat2.run_model(image, FakeAdapter())
        expected = np.repeat(np.repeat(image.astype(np.float32) / 255, 4, 0), 4, 1)
        self.assertEqual(out.shape, expected.shape)
        self.assertLess(np.abs(out - expected).max(), 1e-5)

    @unittest.skipUnless(HAVE_CV2, 'needs OpenCV (requirements-dat2.txt)')
    def test_tta4_and_chain_are_orientation_consistent(self):
        rng = np.random.default_rng(2)
        image = rng.integers(0, 256, (24, 30, 3), dtype=np.uint8)
        expected = np.repeat(np.repeat(image.astype(np.float32) / 255, 4, 0), 4, 1)
        self.assertLess(np.abs(dat2.run_tta4(image, FakeAdapter()) - expected).max(), 1e-5)
        self.assertEqual(dat2.run_chain(image, FakeAdapter()).shape, expected.shape)

    @unittest.skipUnless(HAVE_CV2, 'needs OpenCV (requirements-dat2.txt)')
    def test_frame_inference_covers_every_assembly_input(self):
        base = sprite((20, 18), 200, (150, 120, 90))
        team = sprite((20, 18), 150, (0, 190, 0), True)
        models = dat2.VARIANTS['bhi-nomos']['models']
        out = dat2.infer_frame(base, team, FakeAdapter(), models)
        keys = {f'{m}/{mode}/{k}' for m in models for mode in ('chain', 'tta4') for k in ('base', 'team', 'comp')}
        keys |= {f'{dat2.ALPHA_MODEL}/chain/{k}' for k in ('abase', 'ateam')}
        self.assertEqual(set(out), keys)
        for value in out.values():
            self.assertEqual((value.shape[:2], value.dtype), ((72, 80), np.float16))
        self.assertNotIn(f'{dat2.ALPHA_MODEL}/chain/ateam', dat2.infer_frame(base, None, FakeAdapter()))

    @unittest.skipUnless(HAVE_CV2, 'assembly needs OpenCV')
    def test_smaller_team_layer_shares_the_base_origin(self):
        base = sprite((24, 20), 200, (150, 120, 90))
        team = sprite((21, 17), 150, (0, 190, 0), True)
        inference = dat2.infer_frame(base, team, FakeAdapter(), dat2.VARIANTS['bhi']['models'])
        out_b, out_t = dat2.assemble(base, team, inference, dat2.variant_recipe('bhi'))
        self.assertEqual(out_b.shape[:2], (80, 96))
        self.assertEqual(out_t.shape[:2], (68, 84))
        with self.assertRaises(ValueError):
            dat2.team_on_base_canvas(team, base)

    def test_model_scale_is_checked(self):
        adapter = FakeAdapter()
        adapter.scale = 2
        with self.assertRaises(ValueError):
            dat2.infer_frame(sprite((20, 18), 200, (1, 2, 3)), None, adapter)

    def test_gpu_lock_breaks_stale_lock_and_cleans_up(self):
        with tempfile.TemporaryDirectory() as tmp:
            lock = Path(tmp) / '.gpu.lock'
            lock.mkdir()
            (lock / 'pid').write_text('999999\n')  # no such process
            with dat2.GpuLock(lock):
                self.assertEqual(int((lock / 'pid').read_text()), dat2.os.getpid())
            self.assertFalse(lock.exists())


@unittest.skipUnless(HAVE_CV2, 'assembly needs OpenCV (tools/artwork/ai/requirements-dat2.txt)')
class StagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Repo(Path(self.temp.name) / 'repo')
        self.output = Path(self.temp.name) / 'staging'

    def generate(self, frames=('inn0b0', 'inn0c0'), adapter=FakeAdapter):
        return dat2.generate(self.repo.root, list(frames), self.output, None, adapter_factory=adapter,
                             log=lambda *_: None)

    def test_generates_both_layers_with_provenance_without_touching_the_repo(self):
        before = self.repo.snapshot()
        run = self.generate()
        self.assertEqual(before, self.repo.snapshot())
        self.assertEqual(run['frames'], ['inn0b0', 'inn0c0'])
        self.assertEqual(sorted(p.name for p in self.output.glob('*.png')), ['inn0b0.png', 'inn0b0r.png', 'inn0c0.png'])
        record = json.loads((self.output / 'provenance/inn0b0.json').read_text())
        self.assertEqual(record['recipe_name'], dat2.VARIANTS['bhi']['name'])
        self.assertEqual(set(record['models']), {'4xBHI_dat2_real'})
        self.assertEqual({m: r['sha256'] for m, r in record['models'].items()},
                         {m: dat2.MODELS[m]['sha256'] for m in record['models']})
        self.assertEqual(record['models']['4xBHI_dat2_real']['licence'], 'CC-BY-4.0')
        self.assertIn('numpy', record['packages'])
        for layer in record['layers']:
            self.assertEqual(layer['classic_sha256'], sha(self.repo.gfx / layer['file']))
            self.assertEqual(layer['candidate_sha256'], sha(self.output / layer['file']))
            with Image.open(self.output / layer['file']) as image:
                self.assertEqual((image.mode, image.size), ('RGBA', (80, 72)))
                alpha = np.asarray(image)[..., 3]
            classic = np.asarray(Image.open(self.repo.gfx / layer['file']))[..., 3]
            outside = np.kron(classic == 0, np.ones((4, 4), bool))
            from scipy.ndimage import binary_erosion
            self.assertEqual(alpha[binary_erosion(outside, iterations=8)].max(), 0)

    def test_weighted_run_records_weights_and_matches_dropping_zero_models(self):
        spec = dat2.weighted_spec({'bhi': 0.0, 'nomos': 0.5, 'atd': 0.5})
        run = dat2.generate(self.repo.root, ['inn0b0'], self.output, None,
                                                 adapter_factory=FakeAdapter, log=lambda *_: None, variant=spec)
        self.assertEqual((run['variant'], run['recipe_name']), ('weighted', dat2.WEIGHTED_NAME))
        self.assertEqual(run['weights'], {'4xNomos8kDAT': 0.5, '4xRealWebPhoto_v3_atd': 0.5})
        record = json.loads((self.output / 'provenance/inn0b0.json').read_text())
        self.assertEqual(record['weights'], run['weights'])
        self.assertEqual(set(record['models']), {'4xNomos8kDAT', '4xRealWebPhoto_v3_atd'})
        dropped = Path(self.temp.name) / 'dropped'  # same models, equal weights, as a named variant
        dat2.generate(self.repo.root, ['inn0b0'], dropped, None, adapter_factory=FakeAdapter,
                      log=lambda *_: None, variant='nomos-atd')
        for name in ('inn0b0.png', 'inn0b0r.png'):
            self.assertEqual((self.output / name).read_bytes(), (dropped / name).read_bytes())

    def test_classic_rev_reads_committed_layers_not_the_working_tree(self):
        import subprocess
        git = lambda *args: subprocess.run(['git', *args], cwd=self.repo.root, check=True, capture_output=True)
        git('init', '-q')
        git('add', 'content/data/gfx')
        git('-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-qm', 'classics')
        committed = sha(self.repo.gfx / 'inn0b0.png')
        Image.fromarray(sprite((20, 18), 90, (9, 9, 9)), 'RGBA').save(self.repo.gfx / 'inn0b0.png')
        run = dat2.generate(self.repo.root, ['inn0b0'], self.output, None, adapter_factory=FakeAdapter,
                            log=lambda *_: None, classic_rev='HEAD')
        self.assertEqual(len(run['classic_source']['commit']), 40)
        record = json.loads((self.output / 'provenance/inn0b0.json').read_text())
        self.assertEqual(record['layers'][0]['classic_sha256'], committed)
        with self.assertRaises(ValueError):  # the working tree moved on: promotion refuses
            dat2.promote(self.output, self.repo.root, log=lambda *_: None)

    def test_failed_inference_publishes_nothing(self):
        with self.assertRaisesRegex(ValueError, 'failed, nothing published'):
            self.generate(adapter=lambda: FakeAdapter(fail_on=15))
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.output.parent.glob('staging-staging-*')))

    def run_logged(self, output=None, cache=None, resume_from=(), adapter=FakeAdapter, variant='bhi'):
        lines = []
        run = dat2.generate(self.repo.root, ['inn0b0', 'inn0c0'], output or self.output, None,
                            adapter_factory=adapter, log=lines.append, cache=cache, resume_from=resume_from,
                            variant=variant)
        return run, lines

    def test_one_failing_frame_keeps_the_others_and_a_rerun_computes_only_it(self):
        real = dat2.assemble

        def flaky(base, team, *args):
            if team is None:  # inn0c0
                raise RuntimeError('injected failure')
            return real(base, team, *args)
        lines = []
        with patch.object(dat2, 'assemble', side_effect=flaky):
            with self.assertRaisesRegex(ValueError, 'inn0c0'):
                dat2.generate(self.repo.root, ['inn0b0', 'inn0c0'], self.output, None, adapter_factory=FakeAdapter,
                              log=lines.append, variant='bhi')
        self.assertFalse(self.output.exists())
        self.assertTrue(any('FAILED inn0c0' in line for line in lines))
        cache = self.output.parent / 'staging.frames'
        cached = {p.name: p.read_bytes() for p in (cache / 'inn0b0').glob('*.png')}
        self.assertEqual(sorted(cached), ['inn0b0.png', 'inn0b0r.png'])
        adapters = []
        run, lines = self.run_logged(adapter=lambda: adapters.append(FakeAdapter()) or adapters[-1])
        self.assertIn('reused 1, computing 1', lines[0])
        self.assertEqual((run['reused'], run['computed']), (['inn0b0'], ['inn0c0']))
        for name, data in cached.items():
            self.assertEqual((self.output / name).read_bytes(), data)  # byte-for-byte, no recompute
        full = Path(self.temp.name) / 'full'
        self.run_logged(full, cache=Path(self.temp.name) / 'fresh-cache')
        for name in ('inn0b0.png', 'inn0b0r.png', 'inn0c0.png'):
            self.assertEqual((self.output / name).read_bytes(), (full / name).read_bytes())

    def test_rerun_reuses_all_without_inference(self):
        self.run_logged()
        again = Path(self.temp.name) / 'again'
        run, lines = self.run_logged(again, cache=self.output.parent / 'staging.frames',
                                     adapter=lambda: FakeAdapter(fail_on=1))  # any inference would fail
        self.assertIn('reused 2, computing 0', lines[0])
        self.assertEqual(run['computed'], [])

    def test_key_mismatch_and_corrupted_entries_are_recomputed(self):
        self.run_logged()
        cache = self.output.parent / 'staging.frames'
        _, lines = self.run_logged(Path(self.temp.name) / 'other', cache=cache, variant='atd')
        self.assertIn('reused 0, computing 2', lines[0])  # different recipe and models
        _, lines = self.run_logged(Path(self.temp.name) / 'same', cache=cache, variant='atd')
        self.assertIn('reused 2, computing 0', lines[0])
        with open(cache / 'inn0b0' / 'inn0b0r.png', 'r+b') as handle:
            handle.seek(60)
            handle.write(b'corrupt')
        _, lines = self.run_logged(Path(self.temp.name) / 'repaired', cache=cache, variant='atd')
        self.assertIn('reused 1, computing 1', lines[0])

    def test_resume_from_imports_a_staging_dir_and_rejects_other_classics(self):
        self.run_logged()
        imported = Path(self.temp.name) / 'imported'
        run, lines = self.run_logged(imported, cache=Path(self.temp.name) / 'cache2', resume_from=[self.output],
                                     adapter=lambda: FakeAdapter(fail_on=1))
        self.assertIn('reused 2, computing 0', lines[0])
        for name in ('inn0b0.png', 'inn0b0r.png', 'inn0c0.png'):
            self.assertEqual((imported / name).read_bytes(), (self.output / name).read_bytes())
        Image.fromarray(sprite((20, 18), 90, (9, 9, 9)), 'RGBA').save(self.repo.gfx / 'inn0c0.png')
        _, lines = self.run_logged(Path(self.temp.name) / 'changed', cache=Path(self.temp.name) / 'cache3',
                                   resume_from=[self.output])
        self.assertIn('reused 1, computing 1', lines[0])

    def test_rejects_excluded_frames_before_writing(self):
        for frames in (['school1b0'], ['wall0'], ['inn0b0', 'unknown']):
            with self.assertRaises(ValueError):
                self.generate(frames)
            self.assertFalse(self.output.exists())


@unittest.skipUnless(HAVE_CV2, 'staging needs OpenCV (tools/artwork/ai/requirements-dat2.txt)')
class PromotionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Repo(Path(self.temp.name) / 'repo')
        self.staging = Path(self.temp.name) / 'staging'
        dat2.generate(self.repo.root, ['inn0b0', 'inn0c0'], self.staging, None, adapter_factory=FakeAdapter,
                      log=lambda *_: None, variant=dat2.weighted_spec('bhi=0.75,atd=0.25'))

    def promote(self, **kwargs):
        return dat2.promote(self.staging, self.repo.root, log=lambda *_: None, **kwargs)

    def test_dry_run_writes_nothing(self):
        before = self.repo.snapshot()
        self.assertEqual([frame for frame, _, _ in self.promote(dry_run=True)], ['inn0b0', 'inn0c0'])
        self.assertEqual(before, self.repo.snapshot())

    def test_promotes_base_and_team_layers_and_keeps_the_pack_valid(self):
        from tools.artwork import package_runtime
        self.promote(skip={'inn0c0'})
        manifest = json.loads((self.repo.production / 'pack-metadata/manifest.json').read_text())
        frames = {f['id']: f for f in manifest['frames']}
        self.assertEqual(frames['inn0b0']['recipe'], dat2.WEIGHTED_NAME)
        self.assertEqual(frames['inn0c0']['recipe'], Repo.FRAMES['inn0c0'][0])
        self.assertEqual([l['role'] for l in frames['inn0b0']['layers']], ['base', 'team'])
        for name in ('inn0b0.png', 'inn0b0r.png'):
            staged = (self.staging / name).read_bytes()
            self.assertEqual((self.repo.production / 'ai-upscaled' / name).read_bytes(), staged)
            self.assertEqual((self.repo.runtime / name).read_bytes(), staged)
        self.assertEqual(frames['inn0b0']['sources'][0]['path'], 'tools/artwork/ai/dat2_upscale.py')
        self.assertEqual([(m['name'], m['licence']) for m in frames['inn0b0']['models']],
                         [('4xBHI_dat2_real', 'CC-BY-4.0'), ('4xRealWebPhoto_v3_atd', 'CC-BY-4.0')])
        self.assertEqual((self.repo.runtime / 'manifest.json').read_bytes(),
                         (self.repo.production / 'pack-metadata/manifest.json').read_bytes())
        self.assertIn('inn0b0 20 18 4 inn0b0.png inn0b0r.png',
                      (self.repo.runtime / 'frames.txt').read_text().splitlines())
        package_runtime.validate(self.repo.production)  # every index hash and category consistent
        self.assertEqual(frames['inn0b0']['weights'], {'4xBHI_dat2_real': 0.75, '4xRealWebPhoto_v3_atd': 0.25})
        self.promote()  # a promoted frame can be re-promoted from a newer run
        package_runtime.validate(self.repo.production)

    def test_rejects_stale_classic_tampered_candidate_and_unknown_frames(self):
        before = self.repo.snapshot()
        with self.assertRaises(ValueError):
            self.promote(only={'inn0b0', 'nope'})
        Image.new('RGBA', (20, 18)).save(self.staging / 'inn0c0.png')
        with self.assertRaises(ValueError):
            self.promote()
        self.assertEqual(before, self.repo.snapshot())

    def test_weighted_run_promotes_with_its_weights(self):
        from tools.artwork import package_runtime
        weighted = Path(self.temp.name) / 'weighted'
        dat2.generate(self.repo.root, ['inn0b0'], weighted, None, adapter_factory=FakeAdapter,
                      log=lambda *_: None, variant=dat2.weighted_spec('bhi=0.5,nomos=0.3,atd=0.2'))
        dat2.promote(weighted, self.repo.root, log=lambda *_: None)
        manifest = json.loads((self.repo.production / 'pack-metadata/manifest.json').read_text())
        entry = next(f for f in manifest['frames'] if f['id'] == 'inn0b0')
        self.assertEqual(entry['recipe'], dat2.WEIGHTED_NAME)
        self.assertEqual(entry['weights'], {'4xBHI_dat2_real': 0.5, '4xNomos8kDAT': 0.3, '4xRealWebPhoto_v3_atd': 0.2})
        package_runtime.validate(self.repo.production)

    def test_reference_only_variant_cannot_be_promoted(self):
        reference = Path(self.temp.name) / 'reference'
        dat2.generate(self.repo.root, ['inn0b0'], reference, None, adapter_factory=FakeAdapter,
                      log=lambda *_: None, variant='usv2-bhi')
        with self.assertRaisesRegex(ValueError, 'reference-only'):
            dat2.promote(reference, self.repo.root, dry_run=True, log=lambda *_: None)

    def test_rejects_changed_classic(self):
        Image.new('RGBA', (20, 18), (1, 2, 3, 255)).save(self.repo.gfx / 'inn0b0r.png')
        before = self.repo.snapshot()
        with self.assertRaises(ValueError):
            self.promote(only={'inn0b0'})
        self.assertEqual(before, self.repo.snapshot())


class SharpenSkipTests(unittest.TestCase):
    def test_sharpen_rerun_leaves_dat2_frames_alone(self):
        from tools.artwork import sharpen_upscales as sharpen
        image = Image.new('RGBA', (8, 8), (100, 100, 100, 255))
        import io
        data = io.BytesIO()
        image.save(data, 'PNG')
        sources = [('inn0b0', 'outline_repair', data.getvalue()), ('hosp0b0', 'painted_repair', data.getvalue())]
        recipes = {'inn0b0': dat2.VARIANTS['bhi']['name'], 'hosp0b0': 'painted_repair; colour sharpening v1'}
        with patch.object(sharpen, 'sources', return_value=sources), \
                patch.object(sharpen, 'registered', side_effect=lambda f: {'recipe': recipes[f]}), \
                patch.object(sharpen, 'source_record', return_value={}), \
                patch.object(sharpen, 'rebuild_resource_atlas'), \
                patch.object(sharpen, 'register_frames') as register:
            sharpen.main([])
        self.assertEqual([f['id'] for f in register.call_args[0][0]], ['hosp0b0'])


if __name__ == '__main__':
    unittest.main()
