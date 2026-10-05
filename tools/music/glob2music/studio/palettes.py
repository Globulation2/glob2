# SPDX-License-Identifier: GPL-3.0-or-later
"""Versioned palettes. Assets are installed at image-build time, never by a job."""

import importlib.util
from pathlib import Path
from ..backends.sfizz import SfizzBackend, PATCHES
from ..backends.surge import SurgeBackend
from ..backends.dsp import DspBackend
from ..score import Instrument

SETS = Path(__file__).resolve().parents[2] / "sets"


def module(path):
    spec = importlib.util.spec_from_file_location("studio_palette", path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def synth_players():
    patches = module(SETS / "glass-garden/patches.py")
    return {
        "glass_lead": (
            Instrument("glass_lead", "sustain", low=55, high=100),
            patches.GLASS_LEAD,
        ),
        "reed_counter": (
            Instrument("reed_counter", "sustain", low=46, high=84),
            patches.REED_COUNTER,
        ),
        "glass_pad": (
            Instrument("glass_pad", "sustain", low=48, high=79),
            patches.GLASS_PAD,
        ),
        "round_bass": (
            Instrument("round_bass", "sustain", low=28, high=55),
            patches.ROUND_BASS,
        ),
        "pulse_pluck": (
            Instrument("pulse_pluck", "short", low=55, high=96),
            patches.PULSE_PLUCK,
        ),
    }


DSP = {
    "kalimba": (Instrument("kalimba", "ring", low=60, high=96), "kalimba"),
    "bloop": (Instrument("bloop", "ring", low=60, high=96), "bloop"),
    "kit": (Instrument("kit", "perc", low=35, high=77), "kit"),
}


def instruments(pipeline):
    if pipeline == "acoustic-v1":
        # Use the union of the four approved acoustic sets' pinned instruments.
        from ..backends.sfizz import read_lock

        locked = {
            (e.library, e.path)
            for name in ("moss-lanterns", "thistle-waltz", "bramble-jig", "fennel-mist")
            for e in read_lock(SETS / name / "samples.lock")
        }
        return {
            k: p.instrument for k, p in PATCHES.items() if (p.library, p.sfz) in locked
        }
    if pipeline == "synth-v1":
        return {k: v[0] for k, v in {**synth_players(), **DSP}.items()}
    raise ValueError("Unknown pipeline version")


class Backend:
    def __init__(self, pipeline, cache, work, progress=None):
        self.instruments = instruments(pipeline)
        self.progress = progress or (lambda **_: None)
        if pipeline == "acoustic-v1":
            from ..backends.sfizz import read_lock, write_lock

            entries = {}
            for name in (
                "moss-lanterns",
                "thistle-waltz",
                "bramble-jig",
                "fennel-mist",
            ):
                for e in read_lock(SETS / name / "samples.lock"):
                    entries[(e.library, e.path)] = e
            lock = work / "samples.lock"
            write_lock(lock, list(entries.values()))
            self.backends = [SfizzBackend(lock, cache, offline=True, concurrency=2)]
        else:
            self.backends = [
                SurgeBackend(synth_players(), cache_dir=cache, offline=True),
                DspBackend(DSP),
            ]
        self.preroll_s = max(b.preroll_s for b in self.backends)

    def render(self, performed, seconds, work):
        result = {m: {} for m in performed}
        for backend in self.backends:
            for mood, parts in performed.items():
                for part in parts:
                    if part.instrument not in backend.instruments:
                        continue
                    self.progress(stage="render", detail=f"{mood}: {part.name}")
                    stem = backend.render({mood: [part]}, seconds, work)[mood][
                        part.name
                    ]
                    if backend.preroll_s != self.preroll_s:
                        import numpy as np

                        stem = np.pad(
                            stem,
                            (
                                (
                                    round((self.preroll_s - backend.preroll_s) * 48000),
                                    0,
                                ),
                                (0, 0),
                            ),
                        )
                    result[mood][part.name] = stem
        return result
