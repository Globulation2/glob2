# SPDX-License-Identifier: GPL-3.0-or-later
"""The symbolic music layer: compose a score once, orchestrate it three ways, perform it.

Pipeline role: symbolic sets (``method = "symbolic-acoustic"`` or ``"symbolic-synth"``)
are written as code in ``sets/<id>/composition.py`` and built by
``pipeline.build_trio`` with a rendering backend (``backends/sfizz.py`` for sample
libraries). The layer is backend-neutral: a backend only has to accept
``perform.PerformedPart`` objects and return stems.

Modules, in the order music flows through them:

=============  ===========================================================================
notation       text notation for voices and chord charts; ``Note``; chord theory, voicing
model          ``Score`` (form, harmony, voices, dynamic arc, tempo map), ``Part``,
               ``Instrument``, ``TempoMap``
counter        constraint-based counter-line drafting and hand-written overrides
gen            ``Accompaniment``: rolled/broken chords, pads, pizzicato, walking and
               galloping basses, ostinati, percussion and timpani from the harmony
checks         static checks (ranges, parallels, crossings, repeated bars, non-chord
               tones, density) and the rendered-stem dropout scan
perform        humanisation and articulation -> ``PerformedPart`` (events + CC11)
midi           renderable performance MIDI and a readable score MIDI
mix            levels, stem filters, pan, circular reverb, bus EQ/compression
pipeline       ``build_trio``: all of the above for a composition and a backend
=============  ===========================================================================

Writing a new score: copy a set's ``composition.py``, change the chord chart, voices,
intensity arc and ``arrange``; run the build; read ``build/<id>/score-checks.txt``.
"""
from .checks import Finding, check_score, find_dropouts
from .counter import counter_line, override
from .gen import Accompaniment
from .model import Instrument, Part, Score, TempoMap, fold_into_range, octave
from .notation import Note, chord_at, chord_pcs, name_to_midi, parse_harmony, parse_line, voice_lead
from .perform import PerformedPart, perform_part, player_seed, seed_namespace
from .pipeline import build_trio, load_composition

__all__ = [
    'Accompaniment', 'Finding', 'Instrument', 'Note', 'Part', 'PerformedPart', 'Score', 'TempoMap',
    'build_trio', 'check_score', 'chord_at', 'chord_pcs', 'counter_line', 'find_dropouts', 'fold_into_range',
    'load_composition', 'name_to_midi', 'octave', 'override', 'parse_harmony', 'parse_line', 'perform_part',
    'player_seed', 'seed_namespace', 'voice_lead',
]
