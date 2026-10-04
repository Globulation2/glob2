# SPDX-License-Identifier: GPL-3.0-or-later
"""Recipe: the composition in ``composition.py`` with its own (approved)
performance, rendered on the Surge XT patches in ``patches.py`` (``backends/surge.py``)
and the NumPy kalimba, droplets and drum kit (``backends/dsp.py``), and mixed by
``mixdown.py``. Both per-set hooks of ``score.pipeline.build_trio`` are used:
``perform_fn`` (the composition's humanisation) and ``mix_fn`` (the set's mix chain).
Mastering is the shared ``master.finish``.

The ``Instrument`` entries here give the score checks each player's family and range;
mix seats live in ``mixdown.MIX``.
"""
import importlib.util

from glob2music.backends.dsp import DspBackend
from glob2music.backends.surge import SurgeBackend
from glob2music.score import Instrument, build_trio, load_composition


def _module(ctx, name):
    spec = importlib.util.spec_from_file_location(f'glass_garden_{name}', ctx.path(f'{name}.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def surge_instruments(patches):
    """Melodic players on Surge: ``{key: (Instrument, SurgePatch)}``."""
    return {
        'glass_lead': (Instrument('glass_lead', 'sustain', low=55, high=100), patches.GLASS_LEAD),
        'reed_counter': (Instrument('reed_counter', 'sustain', low=46, high=84), patches.REED_COUNTER),
        'glass_pad': (Instrument('glass_pad', 'sustain', low=48, high=79), patches.GLASS_PAD),
        'round_bass': (Instrument('round_bass', 'sustain', low=28, high=55), patches.ROUND_BASS),
        'pulse_pluck': (Instrument('pulse_pluck', 'short', low=55, high=96), patches.PULSE_PLUCK),
    }


#: Players on the NumPy voices: ``{key: (Instrument, voice)}``.
DSP_INSTRUMENTS = {
    'kalimba': (Instrument('kalimba', 'ring', low=60, high=96), 'kalimba'),
    'bloop': (Instrument('bloop', 'ring', low=60, high=96), 'bloop'),
    'kit': (Instrument('kit', 'perc', low=35, high=77), 'kit'),
}


class GlassGardenBackend:
    """Sends each part to the backend that owns its instrument; both use the same
    pre-roll, so the stems line up for ``loop.fold_tail``."""

    def __init__(self, surge, dsp):
        assert surge.preroll_s == dsp.preroll_s
        self.surge, self.dsp = surge, dsp
        self.instruments = {**surge.instruments, **dsp.instruments}
        self.preroll_s = surge.preroll_s

    def render(self, performed, loop_seconds, work_dir):
        stems = {}
        for backend in (self.surge, self.dsp):
            mine = {m: [p for p in ps if p.instrument in backend.instruments] for m, ps in performed.items()}
            for mood, parts in backend.render(mine, loop_seconds, work_dir).items():
                stems.setdefault(mood, {}).update(parts)
        return stems


def build(ctx):
    composition = load_composition(ctx.path('composition.py'))
    mixdown = _module(ctx, 'mixdown')
    surge = SurgeBackend(surge_instruments(_module(ctx, 'patches')), automation=composition.MACROS,
                         cache_dir=ctx.cache_dir, offline=ctx.offline)
    trio = build_trio(ctx, composition, GlassGardenBackend(surge, DspBackend(DSP_INSTRUMENTS)),
                      perform_fn=composition.perform_part, mix_fn=lambda folded: mixdown.mix(folded, composition))
    surge.export_patches(ctx.work_dir / 'patches')      # .fxp copies to open in Surge XT
    return trio
