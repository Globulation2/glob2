"""Candidate pipeline failures must not write production or publish partial pairs."""

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from PIL import Image

from tools.artwork.ai import upscale


class CandidateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'repo'
        self.pack = self.root / 'data/highres/v1'
        self.pack.mkdir(parents=True)
        self.native = self.root / 'data/gfx'
        self.native.mkdir(parents=True)
        self.output = Path(self.temp.name) / 'candidate'
        self.frame = dict(id='inn0b0', recipe='current', layers=[])
        for role, name in (('base', 'inn0b0.png'), ('team', 'inn0b0r.png')):
            image = Image.new('RGBA', (3, 2), (60, 80, 100, 0))
            image.putpixel((1, 0), (20, 50, 80, 160))
            image.save(self.native / name)
            self.frame['layers'].append(dict(file=name, role=role, logical_width=3, logical_height=2))
        self.manifest()

    def manifest(self):
        (self.pack / 'manifest.json').write_text(json.dumps(dict(frames=[self.frame])))

    def test_prepare_fills_rgb_and_records_both_layers_without_inference(self):
        before = {p.name: p.read_bytes() for p in self.native.iterdir()}
        with patch.object(upscale.subprocess, 'run', side_effect=AssertionError('Unexpected inference')):
            recipe = upscale.generate(self.root, 'inn0b0', self.output, prepare_only=True)
        self.assertEqual(recipe['stage'], 'prepared')
        self.assertEqual(len(recipe['layers']), 2)
        for layer in recipe['layers']:
            with Image.open(self.output / 'input' / layer['file']) as prepared:
                self.assertEqual(prepared.size, (35, 34))
                self.assertTrue(np.all(np.asarray(prepared) == (20, 50, 80)))
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.native.iterdir()})
        self.assertEqual(list((self.output / 'candidate').iterdir()), [])

    def test_rejects_originals_terrain_unknown_and_missing_layers_before_writes(self):
        for recipe in ('recovered original building: inn', 'quiet ripples; periodic material v3'):
            self.frame['recipe'] = recipe
            self.manifest()
            with self.assertRaises(ValueError):
                upscale.generate(self.root, 'inn0b0', self.output, prepare_only=True)
            self.assertFalse(self.output.exists())
        self.frame['recipe'] = 'current'
        self.frame['layers'].pop()
        self.manifest()
        for frame_id in ('inn0b0', 'unknown'):
            with self.assertRaises(ValueError):
                upscale.generate(self.root, frame_id, self.output, prepare_only=True)
            self.assertFalse(self.output.exists())

    def test_rejects_protected_ancestor_and_symlink_outputs(self):
        for output in (self.root, self.root.parent, self.root / 'src/stage',
                       self.root / 'data/stage', self.root / 'artifacts'):
            with self.assertRaises(ValueError):
                upscale.staging_output(self.root, output)
        link = Path(self.temp.name) / 'linked'
        link.symlink_to(self.root / 'data', target_is_directory=True)
        with self.assertRaises(ValueError):
            upscale.staging_output(self.root, link / 'stage')
        allowed = self.root / 'artifacts/candidates/inn'
        self.assertEqual(upscale.staging_output(self.root, allowed), allowed.resolve())

    def test_rejects_existing_staging_without_overwriting_it(self):
        self.output.mkdir()
        marker = self.output / 'recipe.json'
        marker.write_text('older reviewed result')
        with self.assertRaises(ValueError):
            upscale.generate(self.root, 'inn0b0', self.output, prepare_only=True)
        self.assertEqual(marker.read_text(), 'older reviewed result')

    def test_inference_failure_does_not_publish_partial_pair(self):
        executable = Path(self.temp.name) / 'model'
        executable.touch()
        models = Path(self.temp.name) / 'models'
        models.mkdir()
        for suffix in ('.bin', '.param'):
            (models / (upscale.MODEL + suffix)).write_bytes(b'fake model fixture')
        calls = []

        def infer(command, check):
            calls.append(command)
            if len(calls) == 2:
                raise upscale.subprocess.CalledProcessError(1, command)
            Image.new('RGB', (140, 136), (30, 50, 80)).save(command[command.index('-o') + 1])

        with patch.object(upscale.subprocess, 'run', side_effect=infer):
            with self.assertRaises(upscale.subprocess.CalledProcessError):
                upscale.generate(self.root, 'inn0b0', self.output, executable, models)
        self.assertEqual(len(calls), 2)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.output.parent.glob('candidate-staging-*')))

    def test_finishing_preserves_alpha_and_checks_model_dimensions(self):
        image = Image.open(self.native / 'inn0b0.png').convert('RGBA')
        raw = Image.new('RGB', (140, 136), (90, 120, 150))
        candidate = upscale.finish_candidate(image, raw)
        self.assertEqual(candidate.size, (12, 8))
        expected = image.getchannel('A').resize(candidate.size, Image.Resampling.BILINEAR)
        self.assertEqual(candidate.getchannel('A').tobytes(), expected.tobytes())
        with self.assertRaises(ValueError):
            upscale.finish_candidate(image, Image.new('RGB', (12, 8)))


if __name__ == '__main__':
    unittest.main()
