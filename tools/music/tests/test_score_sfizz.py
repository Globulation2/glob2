# SPDX-License-Identifier: GPL-3.0-or-later
"""The sfizz backend without sfizz or the network: sample locks, the cache and library tree,
the RAM-loading wrapper, the dropout re-render loop (with a fake sfizz_render), the
instrument catalogue, and the consistency of the shipped symbolic sets."""
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from glob2music.backends import sfizz  # noqa: E402
from glob2music.manifest import SETS_DIR  # noqa: E402
from glob2music.score import Part, check_score, load_composition, perform_part, player_seed  # noqa: E402
from glob2music.score.model import KINDS  # noqa: E402
from glob2music.score.pipeline import instrument_keys, read_levels  # noqa: E402
from glob2music.spec import MOODS  # noqa: E402

SYMBOLIC_SETS = ('moss-lanterns', 'thistle-waltz', 'bramble-jig', 'fennel-mist')

#: A stand-in for sfizz_render: renders each note as a sine. The first render of each
#: output path keeps only 0.3 s of every note, imitating the streaming dropout.
FAKE_SFIZZ = '''#!{python}
import sys, os, mido, numpy as np, soundfile as sf
a = sys.argv
sfz, mid, wav = a[a.index('--sfz') + 1], a[a.index('--midi') + 1], a[a.index('--wav') + 1]
assert 'hint_ram_based=1' in open(sfz).read()
first = not os.path.exists(wav.replace('.part.wav', '.seen'))
open(wav.replace('.part.wav', '.seen'), 'w').close()
t, on, y = 0, {{}}, np.zeros((44100 * 8, 2))
for m in mido.MidiFile(mid).tracks[0]:
    t += m.time
    if m.type == 'note_on' and m.velocity:
        on[m.note] = t / 1920
    elif m.type in ('note_off', 'note_on') and m.note in on:
        s, e = on.pop(m.note), t / 1920
        e = min(e, s + 0.3) if first else e
        n = np.arange(int(s * 44100), int(e * 44100))
        y[n, 0] += 0.1 * np.sin(2 * np.pi * 440 * n / 44100)
sf.write(wav, y, 44100, subtype='PCM_16')
'''


def fake_library(root):
    """A one-instrument 'vsco2ce' clone holding FluteSusVib.sfz and its sample."""
    sample_dir = Path(root) / 'Woodwinds' / 'Flute'
    sample_dir.mkdir(parents=True)
    (sample_dir / 'flute A4.wav').write_bytes(b'RIFF fake')
    (Path(root) / 'FluteSusVib.sfz').write_text('<control>\ndefault_path=Woodwinds\\Flute\\\n'
                                                '<region> sample=flute A4.wav lokey=60 hikey=96\n')
    return root


class LockTest(unittest.TestCase):
    def test_lock_seed_materialize_offline(self):
        with tempfile.TemporaryDirectory() as d:
            clone = fake_library(Path(d) / 'clone')
            entries = sfizz.lock_patches(['flute_sus'], {'vsco2ce': clone})
            self.assertEqual(sorted(e.path for e in entries),
                             ['FluteSusVib.sfz', 'Woodwinds/Flute/flute A4.wav'])
            lock = Path(d) / 'samples.lock'
            sfizz.write_lock(lock, entries)
            self.assertEqual(sorted(sfizz.read_lock(lock), key=lambda e: e.path), sorted(entries, key=lambda e: e.path))
            cache = Path(d) / 'cache'
            sfizz.seed_from_clones(entries, {'vsco2ce': clone}, cache_dir=cache)
            sfizz.materialize(entries, cache_dir=cache, offline=True)      # no network needed
            root = sfizz.library_root('vsco2ce', cache)
            self.assertTrue((root / 'Woodwinds/Flute/flute A4.wav').exists())
            self.assertFalse((root / 'FluteSusVib.sfz').is_symlink())      # sfizz needs real paths

    def test_raw_url_is_pinned_and_quoted(self):
        url = sfizz.LIBRARIES['vcsl'].raw_url('Aerophones/Ocarina, Typical - SusVib.sfz')
        self.assertIn(sfizz.LIBRARIES['vcsl'].commit, url)
        self.assertIn('Ocarina%2C%20Typical', url)


class RenderTest(unittest.TestCase):
    def test_wrapper_and_dropout_rerender(self):
        from glob2music.score import Note, Score, parse_harmony
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            clone = fake_library(d / 'clone')
            entries = sfizz.lock_patches(['flute_sus'], {'vsco2ce': clone})
            sfizz.write_lock(d / 'samples.lock', entries)
            sfizz.seed_from_clones(entries, {'vsco2ce': clone}, cache_dir=d / 'cache')
            fake = d / 'sfizz_render'
            fake.write_text(FAKE_SFIZZ.format(python=sys.executable))
            fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
            with mock.patch.dict(os.environ, {'GLOB2MUSIC_SFIZZ_RENDER': str(fake)}):
                backend = sfizz.SfizzBackend(d / 'samples.lock', cache_dir=d / 'cache', offline=True)
            notes = [Note(0, 2, [69]), Note(2, 2, [72])]
            score = Score(title='t', bpm=120, beats_per_bar=4, bars=1, harmony=parse_harmony('C'), melody=notes,
                          counter=[], bass=[], intensity=[0.5], phrases=[(1, 1)])
            pp = perform_part(score, 'calm', Part('flute', 'flute_sus', notes, 'lead'), sfizz.INSTRUMENTS['flute_sus'], 1)
            stems = backend.render({'calm': [pp]}, score.seconds, d / 'work')
            y = stems['calm']['flute']
            self.assertGreater(np.abs(y[int((2.0 + 0.6) * 44100):int((2.0 + 0.8) * 44100)]).max(), 0.05)
            wrapper = backend.sfz_path('flute_sus')
            self.assertEqual(wrapper.name, 'FluteSusVib' + sfizz.RAM_WRAPPER_SUFFIX)
            self.assertIn('#include "FluteSusVib.sfz"', wrapper.read_text())


class CatalogueTest(unittest.TestCase):
    def test_patches_are_well_formed(self):
        for key, patch in sfizz.PATCHES.items():
            self.assertIn(patch.library, sfizz.LIBRARIES)
            self.assertTrue(patch.sfz.endswith('.sfz'))
            self.assertIn(patch.instrument.kind, KINDS)
            self.assertLessEqual(patch.instrument.low, patch.instrument.high, key)
            self.assertEqual(patch.instrument.name, key)


class ShippedSetsTest(unittest.TestCase):
    """The four symbolic sets build from files in the repository alone (plus downloads)."""

    def test_sets_are_consistent(self):
        for set_id in SYMBOLIC_SETS:
            with self.subTest(set_id):
                set_dir = SETS_DIR / set_id
                comp = load_composition(set_dir / 'composition.py')
                parts = {m: [p for p in comp.arrange(m) if p.notes] for m in MOODS}
                errors = [f for f in check_score(comp.SCORE, parts, sfizz.INSTRUMENTS) if f.level == 'error']
                self.assertEqual(errors, [])
                names = {p.name for ps in parts.values() for p in ps}
                self.assertEqual(set(read_levels(set_dir / 'levels.toml')), names)
                locked = {(e.library, e.path) for e in sfizz.read_lock(set_dir / 'samples.lock')}
                for key in instrument_keys(comp):
                    patch = sfizz.PATCHES[key]
                    self.assertIn((patch.library, patch.sfz), locked)
                # each player keeps one humanisation seed in all moods
                seeds = {player_seed(set_id, n) for n in names}
                self.assertEqual(len(seeds), len(names))


if __name__ == '__main__':
    unittest.main()
