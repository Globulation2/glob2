# SPDX-License-Identifier: GPL-3.0-or-later
"""From a composition to an un-mastered ``Trio``: the symbolic sets' shared build.

Pipeline role: a symbolic set's ``recipe.py`` is a few lines that call ``build_trio``
with its ``composition.py`` and a rendering backend. ``build_trio`` then:

1. arranges the score for each mood (``composition.arrange(mood)``);
2. runs the static score checks (``checks.check_score``), writes them to
   ``<work>/score-checks.txt`` and stops on errors (notes a sampler cannot play);
3. writes the readable score MIDI (``<work>/score.mid``);
4. humanises every part (``perform.perform_part``; seeds from the set id);
5. asks the backend to render the performances (``backend.render``);
6. folds each stem's pre-roll and tail into the loop (``loop.fold_tail``);
7. takes the players' level references from the set's ``levels.toml`` (measuring
   and writing it on the first build), and mixes each mood (``mix.mix_mood``).

The returned ``Trio`` is raw: ``manifest.build_set`` masters it with ``master.finish``.

A composition module defines:

* ``SCORE`` - a ``model.Score``;
* ``arrange(mood) -> [model.Part]`` for ``'calm'``, ``'building'``, ``'combat'``;
* optionally ``MIX_ADJUST = {mood: {part name or role: dB}}``, extra balance on top
  of ``mix.ROLE_DB`` and ``mix.MOOD_ROLE_DB``.

Per-set hooks (``build_trio(..., perform_fn=, mix_fn=)``), for a set whose approved
sound comes from its own performance or mix rather than the shared ones (Glass Garden
has its own humanisation and mix chain):

* ``perform_fn(score, mood, part, instrument, seed) -> PerformedPart`` replaces
  ``perform.perform_part``;
* ``mix_fn({mood: [(PerformedPart, Instrument, folded_loop)]}) -> {mood: loop}``
  replaces the ``levels.toml`` references and ``mix.mix_mood`` for all three moods
  at once (a set mix may reference one mood's stem levels in another).

Without hooks the build is exactly the shared one.

A backend provides ``instruments`` (``{key: model.Instrument}``), ``preroll_s`` and
``render(performed, loop_seconds, work_dir) -> {mood: {part: stem}}``.
"""
import importlib.util
from pathlib import Path

from .. import loop
from ..audio import Trio
from ..spec import MOODS, SAMPLE_RATE
from . import checks, midi, mix, perform


def load_composition(path):
    """Import a set's ``composition.py`` and check it defines ``SCORE`` and ``arrange``."""
    path = Path(path)
    spec = importlib.util.spec_from_file_location('glob2music_composition_' + path.parent.name.replace('-', '_'), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not hasattr(module, 'SCORE') or not callable(getattr(module, 'arrange', None)):
        raise ValueError(f'{path}: a composition must define SCORE and arrange(mood)')
    return module


def instrument_keys(composition):
    """Every instrument key any mood of ``composition`` plays."""
    return sorted({p.instrument for mood in MOODS for p in composition.arrange(mood)})


def read_levels(path):
    """``levels.toml``: ``part = reference_dB`` lines (see ``mix.measure_levels``)."""
    import tomllib
    with open(path, 'rb') as f:
        return {k: float(v) for k, v in tomllib.load(f).items()}


def write_levels(path, levels, note=''):
    lines = ['# Level references of each player, dBFS (see glob2music.score.mix.measure_levels).',
             '# Measured once from the renders and frozen so every rebuild mixes identically;',
             '# delete this file to measure again (this changes the mix).']
    if note:
        lines.append(f'# {note}')
    lines += [f'"{k}" = {v!r}' for k, v in sorted(levels.items())]
    Path(path).write_text('\n'.join(lines) + '\n')


def build_trio(ctx, composition, backend, levels_path=None, perform_fn=None, mix_fn=None):
    """Arrange, check, perform, render and mix ``composition``; return the raw ``Trio``.

    ``ctx`` is the recipe's ``BuildContext``; ``composition`` a module from
    ``load_composition``; ``backend`` e.g. ``backends.sfizz.SfizzBackend``.
    ``levels_path`` defaults to ``<set_dir>/levels.toml``. ``perform_fn`` and ``mix_fn``
    are the optional per-set hooks described in the module docstring.
    """
    score = composition.SCORE
    work = Path(ctx.work_dir)
    parts = {mood: [p for p in composition.arrange(mood) if p.notes] for mood in MOODS}

    findings = checks.check_score(score, parts, backend.instruments)
    (work / 'score-checks.txt').write_text('\n'.join(str(f) for f in findings) + '\n')
    errors = [f for f in findings if f.level == 'error']
    for f in findings:
        if f.level == 'warning':
            ctx.log.info('score: %s', f)
    if errors:
        raise ValueError(f'{ctx.set_id}: {len(errors)} score errors, first: {errors[0]}')
    midi.write_score(work / 'score.mid', score, parts)

    namespace = perform.seed_namespace(ctx.set_id, ctx.seed)
    perform_fn = perform_fn or perform.perform_part
    performed = {mood: [perform_fn(score, mood, p, backend.instruments[p.instrument],
                                   perform.player_seed(namespace, p.name))
                        for p in ps] for mood, ps in parts.items()}
    stems = backend.render(performed, score.seconds, work / 'stems')

    frames = score.loop_frames(SAMPLE_RATE)
    preroll = int(round(backend.preroll_s * SAMPLE_RATE))
    folded = {mood: [(pp, backend.instruments[pp.instrument], loop.fold_tail(stems[mood][pp.name], frames, preroll))
                     for pp in performed[mood]] for mood in MOODS}
    if mix_fn:
        moods = mix_fn(folded)
    else:
        levels_path = Path(levels_path) if levels_path else ctx.set_dir / 'levels.toml'
        if levels_path.exists():
            levels = read_levels(levels_path)
        else:
            kinds = {p.name: backend.instruments[p.instrument].kind for ps in parts.values() for p in ps}
            levels = mix.measure_levels(stems, kinds)
            write_levels(levels_path, levels)
            ctx.log.info('measured and froze %d level references in %s', len(levels), levels_path)
        missing = {p.name for ps in parts.values() for p in ps} - set(levels)
        if missing:
            raise ValueError(f'{levels_path}: no level reference for {sorted(missing)}; delete it to re-measure')
        ir = mix.make_ir()
        adjust = getattr(composition, 'MIX_ADJUST', {})
        moods = {mood: mix.mix_mood(mood, folded[mood], levels, ir, adjust.get(mood, {})) for mood in MOODS}
    return Trio(meta={'score': {'title': score.title, 'bpm': score.bpm, 'bars': score.bars,
                                'time_signature': list(score.time_signature), 'seconds': round(score.seconds, 3)},
                      'humanisation_namespace': namespace}, **moods)
