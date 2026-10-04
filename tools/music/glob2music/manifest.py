# SPDX-License-Identifier: GPL-3.0-or-later
"""Set manifests (``set.toml``), the recipe contract and the build context.

Pipeline role: each soundtrack set lives in ``tools/music/sets/<set_id>/`` with

* ``set.toml`` -- what the set is, where its inputs come from, their licences, and
  any QA waivers (schema below);
* ``recipe.py`` -- a module defining ``build(ctx) -> Trio``.

``build_set`` loads both, calls the recipe with a ``BuildContext``, then runs the
shared stages every set goes through: ``master.finish`` -> ``audio.write_trio`` ->
``preview.make`` -> ``qa.run_checks`` (with the manifest's waivers). Recipes never
write Oggs, normalise loudness or limit peaks themselves.

``set.toml`` schema (unknown keys are rejected so typos do not pass silently)::

    title = "Moss Lanterns"            # player-facing name is derived from the id;
                                       # this is for docs and credits
    method = "symbolic-acoustic"       # symbolic-acoustic | symbolic-synth | adapted |
                                       # generated | reference
    license = "CC0-1.0"                # licence of the finished audio
    credits = ["Composed for Globulation 2 ...", "Samples: VSCO 2 CE (CC0)"]
    ai_generated = false               # true requires an [ai] table
    shipped = true                     # installed into data/zik by `install`
    seed = 1                           # base seed for ctx.rng()
    description = "..."                # optional

    [[sources]]                        # zero or more pinned inputs
    name = "woodland-part-1"           # key for ctx.source(name)
    url = "https://opengameart.org/sites/default/files/woodland_music_part_1.zip"
    sha256 = "<64 hex digits>"
    license = "CC-BY-4.0"
    author = "JC Sounds"
    title = "Woodland Music"           # optional; also filename, note

    [master]                           # optional overrides of master.finish arguments
    highpass_hz = 25.0

    [qa.waivers]                       # optional; every value is the written reason
    "loudness.ladder" = "The original inverts the ladder on purpose."

    [ai]                               # required when ai_generated = true
    model = "ACE-Step 1.5"
    disclosure = "Generated locally with ACE-Step 1.5 (MIT weights) ..."

The QA thresholds themselves cannot be overridden per set; see ``spec.py``.
"""
import dataclasses
from dataclasses import dataclass, field
import importlib.util
import json
import logging
from pathlib import Path
import re
import time
import tomllib
import zlib

import numpy as np

from .sources import DEFAULT_CACHE, Source, fetch_source
from .spec import DEFAULT_SPEC, MOODS

MUSIC_ROOT = Path(__file__).resolve().parents[1]          # tools/music
SETS_DIR = MUSIC_ROOT / 'sets'
BUILD_DIR = MUSIC_ROOT / 'build'
OUT_DIR = MUSIC_ROOT / 'out'
REPO_ROOT = MUSIC_ROOT.parents[1]

SET_ID = re.compile(r'^[a-z0-9]+(-[a-z0-9]+)*$')
METHODS = {'symbolic-acoustic', 'symbolic-synth', 'adapted', 'generated', 'reference'}
_TOP_KEYS = {'title', 'method', 'license', 'credits', 'ai_generated', 'shipped', 'seed', 'description',
             'sources', 'master', 'qa', 'ai'}
_SOURCE_KEYS = {'name', 'url', 'sha256', 'license', 'author', 'title', 'filename', 'note'}


class ManifestError(ValueError):
    """``set.toml`` is missing, malformed or inconsistent."""


@dataclass
class SetManifest:
    """A parsed and validated ``set.toml``."""

    set_id: str
    title: str
    method: str
    license: str
    credits: list = field(default_factory=list)
    ai_generated: bool = False
    shipped: bool = False
    seed: int = 0
    description: str = ''
    sources: list = field(default_factory=list)       # [Source]
    master: dict = field(default_factory=dict)
    waivers: dict = field(default_factory=dict)       # {measure-prefix: reason}
    ai: dict = field(default_factory=dict)
    path: Path = None

    def source(self, name):
        for s in self.sources:
            if s.name == name:
                return s
        raise KeyError(f'{self.set_id}: no source named {name!r} in set.toml')


def parse_manifest(data, set_id, path=None):
    """Validate a ``set.toml`` dict and return a ``SetManifest``."""
    where = str(path or set_id)
    if not SET_ID.match(set_id):
        raise ManifestError(f'{where}: set id {set_id!r} must be lower-case words joined by "-"')
    unknown = set(data) - _TOP_KEYS
    if unknown:
        raise ManifestError(f'{where}: unknown keys {sorted(unknown)}')
    for key in ('title', 'method', 'license'):
        if not isinstance(data.get(key), str) or not data[key].strip():
            raise ManifestError(f'{where}: "{key}" is required')
    if data['method'] not in METHODS:
        raise ManifestError(f'{where}: method must be one of {sorted(METHODS)}')
    sources = []
    for i, s in enumerate(data.get('sources', [])):
        extra = set(s) - _SOURCE_KEYS
        if extra:
            raise ManifestError(f'{where}: sources[{i}] has unknown keys {sorted(extra)}')
        for key in ('name', 'url', 'sha256', 'license'):
            if not s.get(key):
                raise ManifestError(f'{where}: sources[{i}] needs "{key}"')
        if not re.fullmatch(r'[0-9a-fA-F]{64}', s['sha256']):
            raise ManifestError(f'{where}: sources[{i}].sha256 must be 64 hex digits')
        sources.append(Source(**s))
    if len({s.name for s in sources}) != len(sources):
        raise ManifestError(f'{where}: duplicate source names')
    qa = data.get('qa', {})
    if set(qa) - {'waivers'}:
        raise ManifestError(f'{where}: [qa] only accepts "waivers" (thresholds live in spec.py)')
    waivers = qa.get('waivers', {})
    for key, reason in waivers.items():
        if not isinstance(reason, str) or len(reason.strip()) < 10:
            raise ManifestError(f'{where}: waiver "{key}" needs a written reason (at least a sentence)')
    ai_generated = bool(data.get('ai_generated', False))
    ai = data.get('ai', {})
    if ai_generated and not ai.get('disclosure'):
        raise ManifestError(f'{where}: ai_generated sets need [ai] with a "disclosure"')
    return SetManifest(set_id=set_id, title=data['title'], method=data['method'], license=data['license'],
                       credits=list(data.get('credits', [])), ai_generated=ai_generated,
                       shipped=bool(data.get('shipped', False)), seed=int(data.get('seed', 0)),
                       description=data.get('description', ''), sources=sources,
                       master=dict(data.get('master', {})), waivers=dict(waivers), ai=dict(ai),
                       path=Path(path) if path else None)


def load_manifest(path_or_id):
    """Load ``sets/<id>/set.toml`` (given an id, a set directory or a toml path)."""
    p = Path(path_or_id)
    if p.suffix == '.toml':
        toml_path = p
    elif p.is_dir():
        toml_path = p / 'set.toml'
    else:
        toml_path = SETS_DIR / str(path_or_id) / 'set.toml'
    if not toml_path.exists():
        raise ManifestError(f'{toml_path}: not found')
    with open(toml_path, 'rb') as f:
        data = tomllib.load(f)
    set_id = toml_path.parent.name if toml_path.name == 'set.toml' else toml_path.stem
    return parse_manifest(data, set_id, toml_path)


def list_sets():
    """All manifests under ``tools/music/sets``, sorted by id."""
    if not SETS_DIR.exists():
        return []
    return [load_manifest(p) for p in sorted(SETS_DIR.glob('*/set.toml'))]


# ----------------------------------------------------------------------------- build context

@dataclass
class BuildContext:
    """Everything a recipe may use; passed to ``recipe.build(ctx)``.

    Attributes:
        set_id, manifest: the set being built.
        set_dir: ``tools/music/sets/<id>`` (scores, patches, notes live here).
        work_dir: ``tools/music/build/<id>``, scratch space for stems and renders
            (git-ignored, safe to delete; recipes may cache expensive renders here).
        out_dir: where the shared stages write a1/a2/a3.ogg, preview.ogg, qa.json.
        cache_dir: ``tools/music/cache`` (downloads, model weights).
        spec: the ``TrioSpec`` (sample rate, targets) in force.
        seed: the manifest seed, or the ``--seed`` override.
        log: a ``logging.Logger`` named ``glob2music.<id>``.
        offline: refuse network access in ``source()``.
        allow_regenerate: accept generated audio (``genai``) whose SHA-256 differs
            from the one the recipe records, i.e. a new, unreviewed piece
            (``build --allow-regenerate``). Off by default, so a mismatch is an error.

    Determinism: recipes must take all randomness from ``ctx.rng(label)``, so the same
    manifest and seed reproduce the same audio.
    """

    set_id: str
    manifest: SetManifest
    set_dir: Path
    work_dir: Path
    out_dir: Path
    cache_dir: Path = DEFAULT_CACHE
    spec: object = DEFAULT_SPEC
    seed: int = 0
    log: logging.Logger = None
    offline: bool = False
    allow_regenerate: bool = False

    def __post_init__(self):
        if self.log is None:
            self.log = logging.getLogger(f'glob2music.{self.set_id}')
        self.work_dir.mkdir(parents=True, exist_ok=True)

    def source(self, name):
        """Local, SHA-256-verified path of the ``[[sources]]`` entry called ``name``."""
        return fetch_source(self.manifest.source(name), cache_dir=self.cache_dir, offline=self.offline)

    def rng(self, label=''):
        """A ``numpy.random.Generator`` seeded from ``seed`` and ``label``; the same
        label always yields the same stream, independent of call order elsewhere."""
        return np.random.default_rng([self.seed, zlib.crc32(label.encode())])

    def path(self, relative):
        """A path inside the set directory."""
        return self.set_dir / relative


def load_recipe(set_dir):
    """Import ``<set_dir>/recipe.py`` as a module and check it defines ``build``."""
    path = Path(set_dir) / 'recipe.py'
    if not path.exists():
        raise ManifestError(f'{path}: not found')
    name = 'glob2music_recipe_' + Path(set_dir).name.replace('-', '_')
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not callable(getattr(module, 'build', None)):
        raise ManifestError(f'{path}: must define build(ctx) -> Trio')
    return module


def set_spec(manifest, spec=DEFAULT_SPEC):
    """The spec one set is mastered and checked against.

    A set's ``[master]`` table may hold ``loudness_offset_db = {mood = dB}`` to move
    a mood off the shared loudness target when a listener approved it that way (e.g.
    a sparse calm that sounds right a decibel quieter). The offset changes the target
    itself, so ``master.finish`` and the QA loudness check agree on it. Offsets are
    limited to the QA loudness tolerance, so no set drifts far from the shared ladder.
    """
    offsets = dict(manifest.master.get('loudness_offset_db', {}))
    if not offsets:
        return spec
    unknown = set(offsets) - set(MOODS)
    if unknown:
        raise ManifestError(f'{manifest.set_id}: loudness_offset_db has unknown moods {sorted(unknown)}')
    if any(abs(float(v)) > spec.qa.lufs_tolerance for v in offsets.values()):
        raise ManifestError(f'{manifest.set_id}: loudness offsets must stay within '
                            f'{spec.qa.lufs_tolerance} LU of the shared target')
    targets = {mood: spec.target_lufs[mood] + float(offsets.get(mood, 0.0)) for mood in MOODS}
    return dataclasses.replace(spec, target_lufs=targets)


def master_options(manifest):
    """``[master]`` options for ``master.finish`` (everything but the loudness offsets)."""
    return {k: v for k, v in manifest.master.items() if k != 'loudness_offset_db'}


def encode_within_ceiling(trio, out, spec=DEFAULT_SPEC, log=None, attempts=3):
    """Encode ``trio`` to ``out`` and keep every decoded file under the QA true peak.

    Lossy encoding changes the waveform, and at low quality settings (the shipped
    Vorbis q2) the decoded true peak can sit above the mastered signal: by about 1 dB
    (1.03 dB at most over the -1.5 dBTP mastering ceiling on the shipped sets). The
    limiter cannot know that in advance, so this measures the decoded files and, for
    any mood above ``spec.qa.true_peak_max_dbtp`` minus a 0.2 dB margin, applies a
    static trim of the excess and re-encodes that mood; only re-encoded moods are
    decoded again. The largest trim on the shipped sets is 0.73 dB (woodland combat),
    which is inaudible and changes loudness by the same amount, inside the QA loudness
    tolerance. A mood still over after ``attempts`` trims raises ``RuntimeError``.
    Returns the trio actually written (with any trims applied and recorded in
    ``meta['encode_trim_db']``).
    """
    from . import master
    from .audio import Trio, read_audio, write_ogg, write_trio
    limit_db = spec.qa.true_peak_max_dbtp - 0.2
    paths = write_trio(trio, out, quality=spec.vorbis_quality)
    moods = {mood: np.asarray(trio[mood]) for mood in MOODS}
    trims = {mood: 0.0 for mood in MOODS}
    changed = list(MOODS)               # moods whose file has not been measured yet
    for attempt in range(attempts + 1):
        over = {}
        for mood in changed:
            decoded, _rate = read_audio(paths[mood])
            excess = master.loop_true_peak_dbtp(decoded) - limit_db
            if excess > 0:
                over[mood] = excess
        if not over:
            break
        if attempt == attempts:
            raise RuntimeError(f'{out}: decoded true peak still over {limit_db:.1f} dBTP after {attempts} trims: '
                               + ', '.join(f'{m} +{e:.2f} dB' for m, e in over.items()))
        for mood, excess in over.items():
            moods[mood] = moods[mood] * 10 ** (-excess / 20)
            trims[mood] -= excess
            write_ogg(moods[mood], paths[mood], quality=spec.vorbis_quality)
            if log:
                log.info('%s: trimmed %.2f dB for the encoded true peak', mood, excess)
        changed = list(over)
    return Trio(sample_rate=trio.sample_rate,
                meta={**trio.meta, 'encode_trim_db': trims}, **moods)


def build_set(set_id, out_dir=None, seed=None, spec=DEFAULT_SPEC, offline=False, preview=True, check=True,
              allow_regenerate=False):
    """Build one set end to end. Returns ``(Trio, Report or None)``.

    Stages: recipe ``build(ctx)`` -> ``master.finish`` (with ``[master]`` options) ->
    ``encode_within_ceiling`` (``audio.write_trio`` plus decoded true-peak trims) ->
    ``preview.make`` -> ``qa.run_checks`` with the set's waivers, written to ``qa.json``
    and ``build.json`` (provenance) in the output. ``allow_regenerate`` is passed to
    the recipe as ``ctx.allow_regenerate``.
    """
    from . import master, preview as preview_mod
    from .audio import Trio
    from .qa import run_checks
    manifest = load_manifest(set_id)
    spec = set_spec(manifest, spec)
    set_dir = manifest.path.parent
    out = Path(out_dir) if out_dir else OUT_DIR / manifest.set_id
    ctx = BuildContext(set_id=manifest.set_id, manifest=manifest, set_dir=set_dir,
                       work_dir=BUILD_DIR / manifest.set_id, out_dir=out, spec=spec,
                       seed=manifest.seed if seed is None else seed, offline=offline,
                       allow_regenerate=allow_regenerate)
    t0 = time.time()
    ctx.log.info('building %s (seed %d)', manifest.set_id, ctx.seed)
    raw = load_recipe(set_dir).build(ctx)
    if not isinstance(raw, Trio):
        raise TypeError(f'{set_id}: recipe.build must return a glob2music.audio.Trio')
    finished = master.finish(raw, spec, **master_options(manifest))
    finished = encode_within_ceiling(finished, out, spec, log=ctx.log)
    if preview:
        preview_mod.make(finished, out / 'preview.ogg')
    report = None
    if check:
        report = run_checks(out, spec=spec, waivers=manifest.waivers)
        (out / 'qa.json').write_text(report.to_json())
    meta = {'set_id': manifest.set_id, 'seed': ctx.seed, 'frames': finished.frames,
            'seconds': round(time.time() - t0, 1), **finished.meta}
    (out / 'build.json').write_text(json.dumps(meta, indent=1, default=str))
    return finished, report
