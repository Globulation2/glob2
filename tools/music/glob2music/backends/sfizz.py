# SPDX-License-Identifier: GPL-3.0-or-later
"""sfizz backend: render performed scores on CC0 sample libraries (VSCO 2 CE, VCSL).

Pipeline role: ``glob2music.score.pipeline.build_trio`` performs a composition and
hands every ``PerformedPart`` to a backend. This backend writes each part as MIDI,
renders it with ``sfizz_render`` (the SFZ player of the sfizz project, BSD-2-Clause)
through the part's SFZ instrument, checks the stem for streaming dropouts and returns
the stems. Mixing and mastering happen elsewhere.

Three things are pinned so a build is reproducible and its inputs are known:

* **sfizz** 1.2.3, built from the official release tarball (URL + SHA-256, fetched via
  ``sources.fetch``) with the render client only. ``GLOB2MUSIC_SFIZZ_RENDER`` may point
  at an existing ``sfizz_render`` binary of the same version instead.
* **Sample libraries** at fixed git commits of their official repositories:
  VSCO 2 Community Edition (``sgossner/VSCO-2-CE``, branch ``SFZ``) and the Versilian
  Community Sample Library (``sgossner/VCSL``, branch ``sfz``), both CC0 1.0 by
  Versilian Studios (Sam Gossner). Whole-library archives are 2-4 GB, so each set
  pins only the files it plays: ``sets/<id>/samples.lock`` lists every SFZ and sample
  file with its SHA-256, and each is fetched from ``raw.githubusercontent.com`` at the
  pinned commit into the shared cache, then hard-linked into a library tree that mirrors
  the repository layout (SFZ files refer to samples by relative path).
* **Patches**: ``PATCHES`` maps the instrument keys compositions use to an SFZ file and
  an ``Instrument`` (articulation family, attack compensation, key range, mix seat).

Rendering faults handled here:

* By default sfizz_render keeps only the head of each sample in memory and streams the
  rest from a background thread. Offline it renders faster than that thread reads, so
  a sustained note can stop after its preloaded head (~0.3 s, the "dropouts" of the
  first Moss Lanterns and Thistle Waltz renders) or be subtly different from run to
  run (up to -32 dBFS sample differences between two renders of the same MIDI).
  The backend therefore plays every patch through a small wrapper SFZ next to it in
  the library tree (``<name>.glob2music.sfz``: ``<control> hint_ram_based=1`` plus an
  ``#include`` of the pinned file), which makes sfizz load whole samples up front.
  Renders are then bit-for-bit reproducible, also when run in parallel.
* As a safety net, renders run at most ``CONCURRENCY`` (12) at a time and every stem is
  scanned afterwards (``score.checks.find_dropouts``); a stem with a silent sustained
  note is re-rendered alone, up to ``RETRIES`` times, and the build stops if it persists.
* Renders are cached by a hash of the MIDI, the wrapper SFZ, the SFZ path, the
  library commit, the locked SHA-256 of the SFZ file and of every sample it plays, and
  the sfizz version and options, so rebuilding a set after a mix change does not
  re-render, while a new library pin or re-locked sample always does.

Developer commands (``python3 -m glob2music.backends.sfizz``, from ``tools/music``)::

    lock SET_ID --clone vsco2ce=PATH --clone vcsl=PATH
        write sets/<id>/samples.lock for the patches the set's composition uses,
        hashing files from local clones checked out at the pinned commits
    seed SET_ID --clone vsco2ce=PATH --clone vcsl=PATH
        copy (hard-link where possible) the locked files from local clones into the
        cache after verifying each hash, instead of downloading them
    sfizz
        build or locate sfizz_render and print its path
"""
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
import hashlib
import logging
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.parse

import numpy as np
import soundfile as sf

from ..score.checks import find_dropouts
from ..score.midi import read_notes, write_performance
from ..score.model import Instrument
from ..sources import DEFAULT_CACHE, fetch, sha256_of, verify
from ..spec import SAMPLE_RATE

log = logging.getLogger('glob2music.sfizz')

SFIZZ_VERSION = '1.2.3'
SFIZZ_URL = f'https://github.com/sfztools/sfizz/releases/download/{SFIZZ_VERSION}/sfizz-{SFIZZ_VERSION}.tar.gz'
SFIZZ_SHA256 = 'a9339eac7620d7f0f6b44bdfe860680fab73e66efad4b5f15b21198dd9436822'
SFIZZ_CMAKE_FLAGS = ['-DCMAKE_BUILD_TYPE=Release', '-DSFIZZ_JACK=OFF', '-DSFIZZ_RENDER=ON', '-DSFIZZ_SHARED=OFF',
                     '-DSFIZZ_TESTS=OFF', '-DSFIZZ_DEMOS=OFF', '-DSFIZZ_DEVTOOLS=OFF', '-DSFIZZ_BENCHMARKS=OFF']
#: sfizz_render options: 256 voices, resampling quality 3, stop at the end-of-track marker.
SFIZZ_RENDER_ARGS = ['-s', str(SAMPLE_RATE), '-p', '256', '-q', '3', '--use-eot']

#: Seconds of silence before the score in every render (room for notes humanised early).
PREROLL_S = 2.0
#: Seconds rendered after the loop end (releases and reverb tails, folded onto the start).
TAIL_S = 9.0
CONCURRENCY = 12
RETRIES = 4
#: Suffix of the generated wrapper SFZ files that load samples into RAM (see above).
RAM_WRAPPER_SUFFIX = '.glob2music.sfz'


@dataclass(frozen=True)
class Library:
    """A sample library pinned to a commit of its official git repository."""

    key: str
    repo: str
    commit: str
    license: str
    title: str

    def raw_url(self, path):
        return f'https://raw.githubusercontent.com/{self.repo}/{self.commit}/{urllib.parse.quote(path)}'


LIBRARIES = {
    'vsco2ce': Library('vsco2ce', 'sgossner/VSCO-2-CE', '6dd651d55dde97fd4028699be9d4481f26917891', 'CC0-1.0',
                       'VSCO 2 Community Edition (Versilian Studios)'),
    'vcsl': Library('vcsl', 'sgossner/VCSL', 'dfcf4a4918771eee884b96ad4493de82ef84daf6', 'CC0-1.0',
                    'Versilian Community Sample Library (Versilian Studios)'),
}


@dataclass(frozen=True)
class Patch:
    """An SFZ instrument file in a library, and how the score layer plays it."""

    library: str
    sfz: str
    instrument: Instrument


def _patch(key, library, sfz, kind, lag, pan, send, gain_db, low, high, highpass_hz=None):
    return key, Patch(library, sfz, Instrument(key, kind, lag, pan, send, gain_db, low, high, highpass_hz))


_EB = 'Aerophones/Edge-blown Aerophones/'
_PI = 'Idiophones/Plucked Idiophones/'
_SI = 'Idiophones/Struck Idiophones/'
_SM = 'Membranophones/Struck Membranophones/'

#: The instrument catalogue. Columns: library, SFZ path, articulation family, attack lag
#: (s), pan, reverb send, gain trim (dB), lowest and highest mapped key, and an optional
#: fixed stem high-pass. Key ranges are the SFZ maps' outer limits.
PATCHES = dict([
    # -- VSCO 2 CE woodwinds and brass
    _patch('flute_sus', 'vsco2ce', 'FluteSusVib.sfz', 'sustain', -0.012, -0.12, 0.30, 0.0, 60, 96),
    _patch('clarinet_sus', 'vsco2ce', 'ClarinetSus.sfz', 'sustain', -0.012, 0.28, 0.30, -1.0, 50, 90),
    _patch('clarinet_stac', 'vsco2ce', 'ClarinetStac.sfz', 'short', 0.0, 0.28, 0.32, -2.0, 50, 89),
    _patch('oboe_sus', 'vsco2ce', 'OboeSusVib.sfz', 'sustain', -0.010, 0.08, 0.28, -1.0, 58, 89),
    _patch('bassoon_sus', 'vsco2ce', 'BassoonSus.sfz', 'sustain', -0.015, 0.15, 0.30, 0.0, 34, 75),
    _patch('bassoon_stac', 'vsco2ce', 'BassoonStac.sfz', 'short', 0.0, 0.38, 0.25, -3.0, 34, 72),
    _patch('horn_sus', 'vsco2ce', 'FHornSus.sfz', 'sustain', -0.025, -0.30, 0.40, -3.0, 33, 77, highpass_hz=60),
    _patch('horn_stac', 'vsco2ce', 'FHornStac.sfz', 'short', -0.005, -0.30, 0.40, -4.0, 33, 77),
    _patch('trombone_sus', 'vsco2ce', 'TromboneSus.sfz', 'sustain', -0.025, 0.20, 0.35, 0.0, 34, 65),
    _patch('trombone_stac', 'vsco2ce', 'TromboneStac.sfz', 'short', 0.0, 0.20, 0.35, 0.0, 34, 70),
    _patch('tuba_sus', 'vsco2ce', 'TubaSus.sfz', 'sustain', -0.030, 0.30, 0.25, 0.0, 29, 62),
    _patch('tuba_stac', 'vsco2ce', 'TubaStac.sfz', 'short', 0.0, 0.30, 0.25, 0.0, 29, 62),
    # -- VSCO 2 CE strings (ensembles)
    _patch('violins_sus', 'vsco2ce', 'ViolinEnsSusVib.sfz', 'sustain', -0.035, -0.42, 0.33, -1.0, 55, 86),
    _patch('violins_sus_q', 'vsco2ce', 'ViolinEnsSusVib-Quiet.sfz', 'sustain', -0.045, -0.42, 0.38, 0.0, 55, 86),
    _patch('violins_pizz', 'vsco2ce', 'ViolinEnsPizz.sfz', 'short', 0.0, -0.40, 0.35, -3.0, 55, 86),
    _patch('violins_spic', 'vsco2ce', 'ViolinEnsSpic.sfz', 'short', 0.0, -0.42, 0.28, 0.0, 55, 86),
    _patch('violas_sus', 'vsco2ce', 'ViolaEnsSusVib.sfz', 'sustain', -0.040, 0.22, 0.35, -3.0, 48, 86),
    _patch('violas_sus_q', 'vsco2ce', 'ViolaEnsSusVib-Quiet.sfz', 'sustain', -0.045, 0.22, 0.38, -3.0, 48, 86),
    _patch('violas_trem', 'vsco2ce', 'ViolaEnsTrem.sfz', 'sustain', -0.020, 0.22, 0.30, 0.0, 48, 86),
    _patch('violas_pizz', 'vsco2ce', 'ViolaEnsPizz.sfz', 'short', 0.0, 0.22, 0.35, 0.0, 48, 86),
    _patch('violas_spic', 'vsco2ce', 'ViolaEnsSpic.sfz', 'short', 0.0, 0.22, 0.25, -3.0, 48, 86),
    _patch('cellos_sus_q', 'vsco2ce', 'CelloEnsSusVib-Quiet.sfz', 'sustain', -0.040, 0.32, 0.30, -2.0, 36, 77),
    _patch('cellos_pizz', 'vsco2ce', 'CelloEnsPizz.sfz', 'short', 0.0, 0.32, 0.28, -2.0, 36, 77),
    _patch('cellos_spic', 'vsco2ce', 'CelloEnsSpic.sfz', 'short', 0.0, 0.32, 0.22, -2.0, 36, 77),
    _patch('basses_sus', 'vsco2ce', 'ContrabassSusVB.sfz', 'sustain', -0.045, 0.45, 0.22, 0.0, 24, 60),
    _patch('basses_sus_q', 'vsco2ce', 'ContrabassSusVB-Quiet.sfz', 'sustain', -0.045, 0.45, 0.25, -4.0, 24, 60),
    _patch('basses_pizz', 'vsco2ce', 'ContrabassPizz.sfz', 'short', 0.0, 0.45, 0.25, -4.0, 24, 60),
    _patch('basses_spic', 'vsco2ce', 'ContrabassSpic.sfz', 'short', 0.0, 0.45, 0.20, -4.0, 24, 60),
    # -- VSCO 2 CE plucked, mallets and percussion
    _patch('harp', 'vsco2ce', 'Harp.sfz', 'ring', 0.0, -0.58, 0.40, 0.0, 28, 101),
    _patch('glockenspiel', 'vsco2ce', 'Glockenspiel.sfz', 'ring', 0.0, -0.35, 0.45, -8.0, 67, 96),
    _patch('timpani', 'vsco2ce', 'Timpani.sfz', 'perc', 0.0, 0.08, 0.35, -3.0, 36, 60, highpass_hz=26),
    _patch('timpani_roll', 'vsco2ce', 'TimpaniRolls.sfz', 'sustain', 0.0, 0.08, 0.35, -5.0, 36, 60, highpass_hz=26),
    # General-MIDI-style kit map of VSCO percussion (keys in ``PERC_KIT``)
    _patch('perc_kit', 'vsco2ce', 'GM-StylePerc.sfz', 'perc', 0.0, 0.0, 0.25, -4.0, 32, 104),
    # -- VCSL
    _patch('alto_recorder', 'vcsl', _EB + 'Baroque Alto Recorder - SusVib.sfz', 'sustain', -0.008, -0.10, 0.30, 0.0, 65, 91),
    _patch('tenor_recorder', 'vcsl', _EB + 'Baroque Tenor Recorder - SusVib.sfz', 'sustain', -0.008, 0.18, 0.30, 0.0, 60, 86),
    _patch('ocarina', 'vcsl', _EB + 'Ocarina, Typical - SusVib.sfz', 'sustain', -0.008, 0.05, 0.32, 0.0, 69, 86),
    _patch('kalimba', 'vcsl', _PI + 'Kalimba, Tanzania.sfz', 'ring', 0.0, 0.35, 0.35, 0.0, 43, 98),
    _patch('marimba', 'vcsl', _SI + 'Marimba.sfz', 'ring', 0.0, -0.30, 0.30, 0.0, 41, 97),
    _patch('vibraphone_soft', 'vcsl', _SI + 'Vibraphone - Soft Mallets.sfz', 'ring', 0.0, -0.25, 0.40, 0.0, 53, 89),
    _patch('glockenspiel_vcsl', 'vcsl', _SI + 'Glockenspiel.sfz', 'ring', 0.0, -0.35, 0.45, 0.0, 67, 97),
    _patch('folk_harp', 'vcsl', 'Chordophones/Composite Chordophones/Folk Harp.sfz', 'ring', 0.0, -0.50, 0.38, 0.0, 36, 93),
    _patch('frame_drum', 'vcsl', _SM + 'Frame Drum.sfz', 'perc', 0.0, 0.10, 0.25, 0.0, 60, 65),
    _patch('darbuka', 'vcsl', _SM + 'Darbuka.sfz', 'perc', 0.0, 0.30, 0.22, 0.0, 60, 64),
    _patch('bongos', 'vcsl', _SM + 'Bongos.sfz', 'perc', 0.0, -0.30, 0.22, 0.0, 60, 65),
    _patch('tom', 'vcsl', _SM + 'Tom 1.sfz', 'perc', 0.0, -0.15, 0.25, 0.0, 60, 65),
    _patch('snare_rope', 'vcsl', _SM + 'Snare Drum, Rope Tension.sfz', 'perc', 0.0, 0.05, 0.22, 0.0, 60, 65),
    _patch('bass_drum', 'vcsl', _SM + 'Bass Drum 1.sfz', 'perc', 0.0, 0.0, 0.25, 0.0, 60, 60),
    _patch('bass_drum_2', 'vcsl', _SM + 'Bass Drum 2.sfz', 'perc', 0.0, 0.0, 0.25, 0.0, 60, 70),
    _patch('shaker', 'vcsl', _SI + 'Shaker, Small.sfz', 'perc', 0.0, -0.40, 0.20, 0.0, 60, 67),
    _patch('woodblock', 'vcsl', _SI + 'Woodblock.sfz', 'perc', 0.0, 0.40, 0.25, 0.0, 60, 62),
    _patch('slit_drum', 'vcsl', _SI + 'Slit Drum.sfz', 'perc', 0.0, -0.20, 0.28, 0.0, 60, 61),
])

#: Keys of ``perc_kit`` (VSCO 2 CE ``GM-StylePerc.sfz``), with the hand-drum and
#: auxiliary sounds the compositions use.
PERC_KIT = dict(bass_drum=36, snare_tap=37, snare=38, snare_roll=39, tambourine_shake=53, tambourine=54,
                quinto_tap=60, quinto=61, conga_tap=62, conga=63, tumba_tap=64, tumba=65, claves=75,
                log_drum_high=76, log_drum_low=77, triangle_muted=78, triangle=79)

#: The instrument view of the catalogue, for the score layer.
INSTRUMENTS = {key: p.instrument for key, p in PATCHES.items()}


# ----------------------------------------------------------------------------- sfizz binary

def sfizz_render(cache_dir=DEFAULT_CACHE, offline=False):
    """Path of a ``sfizz_render`` binary, building the pinned release if needed.

    ``GLOB2MUSIC_SFIZZ_RENDER`` overrides (it must be sfizz {version}). Otherwise the
    binary lives at ``<cache>/sfizz-{version}/build/library/bin/sfizz_render`` and is
    built on first use (cmake and a C++17 compiler; a few minutes).
    """
    env = os.environ.get('GLOB2MUSIC_SFIZZ_RENDER')
    if env:
        if not os.access(env, os.X_OK):
            raise FileNotFoundError(f'GLOB2MUSIC_SFIZZ_RENDER={env} is not executable')
        return Path(env)
    root = Path(cache_dir) / f'sfizz-{SFIZZ_VERSION}'
    binary = root / 'build' / 'library' / 'bin' / 'sfizz_render'
    if binary.exists():
        return binary
    tarball = fetch(SFIZZ_URL, SFIZZ_SHA256, cache_dir=cache_dir, offline=offline)
    if not (root / 'CMakeLists.txt').exists():
        with tarfile.open(tarball) as tar:
            tar.extractall(root.parent, filter='data')
    log.info('building sfizz_render %s in %s', SFIZZ_VERSION, root)
    subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'build'), *SFIZZ_CMAKE_FLAGS], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(['cmake', '--build', str(root / 'build'), '-j', str(os.cpu_count() or 4),
                    '--target', 'sfizz_render'], check=True, stdout=subprocess.DEVNULL)
    return binary


# ----------------------------------------------------------------------------- sample files

@dataclass(frozen=True)
class LockEntry:
    library: str
    path: str               # repository-relative
    sha256: str
    size: int


def read_lock(path):
    """Parse a ``samples.lock`` (tab-separated: library, sha256, bytes, path)."""
    out = []
    for line in Path(path).read_text().splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        lib, sha, size, rel = line.split('\t')
        if lib not in LIBRARIES:
            raise ValueError(f'{path}: unknown library {lib!r}')
        out.append(LockEntry(lib, rel, sha, int(size)))
    return out


def write_lock(path, entries):
    head = ['# Sample files this set renders with, pinned by SHA-256.',
            '# Generated by `python3 -m glob2music.backends.sfizz lock`; do not edit by hand.',
            '# Libraries (CC0 1.0): ' + '; '.join(f'{k} = {lib.repo}@{lib.commit}' for k, lib in LIBRARIES.items()),
            '# library\tsha256\tbytes\tpath']
    lines = [f'{e.library}\t{e.sha256}\t{e.size}\t{e.path}' for e in sorted(entries, key=lambda e: (e.library, e.path))]
    Path(path).write_text('\n'.join(head + lines) + '\n')


def lock_patches(patch_keys, clones):
    """Lock entries for ``patch_keys``: each SFZ file and every sample it references,
    hashed from ``clones`` (``{library: local checkout at the pinned commit}``)."""
    entries = {}
    for key in sorted(set(patch_keys)):
        patch = PATCHES[key]
        root = Path(clones[patch.library])
        sfz = root / patch.sfz
        sfz_dir = os.path.dirname(patch.sfz)
        files = [patch.sfz] + [os.path.normpath(os.path.join(sfz_dir, s)) for s in _sample_refs(sfz)]
        for rel in files:
            f = root / rel
            if not f.exists():
                raise FileNotFoundError(f'{key}: {f} referenced by {patch.sfz} is missing from the clone')
            entries[(patch.library, rel)] = LockEntry(patch.library, rel, sha256_of(f), f.stat().st_size)
    return list(entries.values())


#: An opcode value runs to the next ``opcode=``, header or end of line (sample names may
#: contain spaces).
_VALUE = r'(.+?)(?=\s+[\w$]+=|\s*<|\s*(?://.*)?$)'


def _sample_refs(sfz_file):
    """Sample paths relative to the SFZ file's directory (``default_path`` applied,
    Windows separators normalised)."""
    text = Path(sfz_file).read_text(errors='ignore')
    m = re.search(r'default_path=' + _VALUE, text, re.M)
    base = m.group(1).strip().replace('\\', '/') if m else ''
    return sorted({os.path.join(base, s.strip().replace('\\', '/'))
                   for s in re.findall(r'\bsample=' + _VALUE, text, re.M)})


def _link(src, dest):
    """Hard-link ``src`` to ``dest`` (copy across file systems). Not a symlink: sfizz
    resolves an SFZ file's sample paths from the file's real location."""
    if dest.is_symlink():
        dest.unlink()
    try:
        os.link(src, dest)
    except OSError:
        shutil.copy2(src, dest)


def library_root(library, cache_dir=DEFAULT_CACHE):
    """Directory mirroring a pinned library's layout (links into the source cache)."""
    lib = LIBRARIES[library]
    return Path(cache_dir) / 'sfizz-libraries' / f'{library}-{lib.commit[:12]}'


def materialize(lock, cache_dir=DEFAULT_CACHE, offline=False):
    """Make every locked file available under ``library_root``: fetch (and verify) it
    into the source cache, then hard-link it at its repository path. Idempotent."""
    for e in lock:
        dest = library_root(e.library, cache_dir) / e.path
        if dest.exists():
            continue
        cached = fetch(LIBRARIES[e.library].raw_url(e.path), e.sha256, cache_dir=cache_dir,
                       filename=os.path.basename(e.path), offline=offline)
        dest.parent.mkdir(parents=True, exist_ok=True)
        _link(cached, dest)


def seed_from_clones(lock, clones, cache_dir=DEFAULT_CACHE):
    """Place locked files from local clones into the source cache (hash-verified,
    hard-linked when on the same file system), so ``materialize`` needs no network."""
    for e in lock:
        dest = Path(cache_dir) / 'sources' / e.sha256[:12].lower() / os.path.basename(e.path)
        if dest.exists():
            continue
        src = Path(clones[e.library]) / e.path
        verify(src, e.sha256)
        dest.parent.mkdir(parents=True, exist_ok=True)
        _link(src, dest)


# ----------------------------------------------------------------------------- rendering

class SfizzBackend:
    """Renders ``PerformedPart`` objects for one set.

    ``lock_path`` is the set's ``samples.lock``; construction fetches and links the
    locked files and locates (or builds) sfizz_render. ``instruments`` is the catalogue
    the score layer checks and performs against.
    """

    instruments = INSTRUMENTS
    preroll_s = PREROLL_S
    tail_s = TAIL_S

    def __init__(self, lock_path, cache_dir=DEFAULT_CACHE, offline=False, concurrency=CONCURRENCY, retries=RETRIES):
        self.lock = read_lock(lock_path)
        self.locked = {(e.library, e.path): e.sha256 for e in self.lock}
        self._pins = {}
        self.cache_dir = Path(cache_dir)
        self.concurrency = concurrency
        self.retries = retries
        materialize(self.lock, cache_dir, offline=offline)
        self.binary = sfizz_render(cache_dir, offline=offline)

    @classmethod
    def for_set(cls, ctx):
        """The backend for a recipe's ``BuildContext``: the set's ``samples.lock``, the
        shared cache, and the build's offline flag."""
        return cls(ctx.path('samples.lock'), cache_dir=ctx.cache_dir, offline=ctx.offline)

    def sfz_path(self, instrument_key):
        """The wrapper SFZ that plays ``instrument_key`` with whole samples in RAM."""
        patch = PATCHES[instrument_key]
        if (patch.library, patch.sfz) not in self.locked:
            raise KeyError(f'{instrument_key}: {patch.sfz} is not in samples.lock; rerun the lock command')
        original = library_root(patch.library, self.cache_dir) / patch.sfz
        wrapper = original.with_name(original.stem + RAM_WRAPPER_SUFFIX)
        text = f'<control>\nhint_ram_based=1\n#include "{original.name}"\n'
        if not wrapper.exists() or wrapper.read_text() != text:
            wrapper.write_text(text)
        return wrapper

    def patch_pins(self, instrument_key):
        """The library commit and the locked SHA-256s of the patch's SFZ file and every
        sample it references, as one string for the render cache key."""
        if instrument_key not in self._pins:
            patch = PATCHES[instrument_key]
            sfz = library_root(patch.library, self.cache_dir) / patch.sfz
            sfz_dir = os.path.dirname(patch.sfz)
            files = [patch.sfz] + [os.path.normpath(os.path.join(sfz_dir, s)) for s in _sample_refs(sfz)]
            hashes = [f'{rel}={self.locked.get((patch.library, rel), "unlocked")}' for rel in files]
            self._pins[instrument_key] = '\n'.join([LIBRARIES[patch.library].commit, *hashes])
        return self._pins[instrument_key]

    def _render_one(self, job):
        sfz, mid, wav, key_file, key = job
        tmp = wav.with_suffix('.part.wav')
        r = subprocess.run([str(self.binary), '--sfz', str(sfz), '--midi', str(mid), '--wav', str(tmp),
                            *SFIZZ_RENDER_ARGS], capture_output=True, text=True)
        if r.returncode != 0 or not tmp.exists():
            raise RuntimeError(f'sfizz_render failed for {mid}: {r.stderr[-800:]}')
        tmp.replace(wav)
        key_file.write_text(key)

    def render(self, performed, loop_seconds, work_dir):
        """Render ``performed`` (``{mood: [PerformedPart]}``) under ``work_dir``.

        Returns ``{mood: {part name: float64 (frames, 2) stem}}``; each stem starts
        ``preroll_s`` before the loop and runs ``tail_s`` past its end (or less, when
        sfizz stops at the last sounding note).
        """
        work_dir = Path(work_dir)
        jobs, todo = [], []
        for mood, parts in performed.items():
            for part in parts:
                d = work_dir / mood
                d.mkdir(parents=True, exist_ok=True)
                mid, wav, key_file = d / f'{part.name}.mid', d / f'{part.name}.wav', d / f'{part.name}.key'
                write_performance(mid, part, loop_seconds, self.preroll_s, self.tail_s)
                sfz = self.sfz_path(part.instrument)
                key = hashlib.sha256(mid.read_bytes() + sfz.read_bytes() + str(PATCHES[part.instrument].sfz).encode()
                                     + self.patch_pins(part.instrument).encode()
                                     + SFIZZ_VERSION.encode() + ' '.join(SFIZZ_RENDER_ARGS).encode()).hexdigest()
                job = (sfz, mid, wav, key_file, key)
                jobs.append(job)
                if not (wav.exists() and key_file.exists() and key_file.read_text() == key):
                    todo.append(job)
        log.info('rendering %d of %d stems with sfizz (%d at a time)', len(todo), len(jobs), self.concurrency)
        with ThreadPoolExecutor(max_workers=self.concurrency) as pool:
            list(pool.map(self._render_one, todo))
        for attempt in range(self.retries + 1):
            bad = [j for j in jobs if self._dropouts(j)]
            if not bad:
                break
            if attempt == self.retries:
                raise RuntimeError(f'dropouts persist after {self.retries} serial re-renders: '
                                   + ', '.join(str(j[2]) for j in bad))
            log.warning('dropout scan pass %d: re-rendering %d stems one at a time', attempt + 1, len(bad))
            for j in bad:
                self._render_one(j)
        stems = {}
        for mood, parts in performed.items():
            stems[mood] = {}
            for part in parts:
                y, sr = sf.read(str(work_dir / mood / f'{part.name}.wav'), always_2d=True)
                if sr != SAMPLE_RATE:
                    raise RuntimeError(f'{part.name}: sfizz wrote {sr} Hz')
                stems[mood][part.name] = np.repeat(y, 2, axis=1) if y.shape[1] == 1 else y
        return stems

    def _dropouts(self, job):
        _, mid, wav, _, _ = job
        y, _ = sf.read(str(wav), always_2d=True)
        silent, _ = find_dropouts(y, read_notes(mid, self.preroll_s), SAMPLE_RATE, self.preroll_s)
        return bool(silent)


# ----------------------------------------------------------------------------- developer CLI

def _main(argv=None):
    import argparse
    from ..manifest import load_manifest
    from ..score.pipeline import load_composition, instrument_keys
    ap = argparse.ArgumentParser(prog='python3 -m glob2music.backends.sfizz')
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name in ('lock', 'seed'):
        p = sub.add_parser(name)
        p.add_argument('set_id')
        p.add_argument('--clone', action='append', default=[], metavar='LIBRARY=PATH')
    sub.add_parser('sfizz')
    args = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format='%(name)s: %(message)s')
    if args.cmd == 'sfizz':
        print(sfizz_render())
        return 0
    clones = dict(c.split('=', 1) for c in args.clone)
    set_dir = load_manifest(args.set_id).path.parent
    lock_path = set_dir / 'samples.lock'
    if args.cmd == 'lock':
        entries = lock_patches(instrument_keys(load_composition(set_dir / 'composition.py')), clones)
        write_lock(lock_path, entries)
        print(f'{lock_path}: {len(entries)} files, {sum(e.size for e in entries) / 1e6:.0f} MB')
    else:
        seed_from_clones(read_lock(lock_path), clones)
        print(f'{args.set_id}: cache seeded from {", ".join(clones)}')
    return 0


if __name__ == '__main__':
    raise SystemExit(_main())
