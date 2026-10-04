# SPDX-License-Identifier: GPL-3.0-or-later
"""Adapting existing recordings into Glob2 mood trios (``method = "adapted"``).

An adapted set starts from human-composed, openly licensed music (CC0, CC BY, ...)
and re-arranges it into calm / building / combat on *one shared timeline*, so the
game can crossfade between moods at any playback position. The steps, and the module
for each:

1. **Get the layers.** ``stems.decode`` the pinned source files. If the composer
   published stems, use them (Woodland). Otherwise ``demucs.separate`` estimates
   them from the mix (Apple Cider, Curious Critters).
2. **Choose the loop.** Recipes pin the loop frames as constants, recorded with how
   they were found (``glob2music.loop.find_loop_points`` and ``refine_loop_end`` for a
   region of a track, or a bar-line ``glob2music.loop.splice`` to shorten a
   composer's loop), so builds are reproducible and the search is an authoring step.
3. **Arrange the moods.** ``stems.remix`` (estimated stems: the mix plus changes) or
   ``stems.sum_stems`` (real stems), then ``stems.cut_loop`` at identical frames for
   every layer.
4. **Shape them.** Loop-aware colour and level: ``glob2music.master`` filters,
   ``stems.tilt_shelf`` / ``widen`` / ``level_ride``, a synthetic ``room``, and
   synthesised ``percussion`` placed on a ``beatgrid`` when the source's own drums
   cannot carry a combat mood.
5. Return the un-mastered ``Trio``; ``manifest.build_set`` masters, encodes and checks
   it like every other set.

The style guide's rules for adapted sets, in practice: calm drops the drums and thins
to a lead and a soft bed; building restores the pulse; combat adds low end and
percussion while staying warm and downtempo. Then a person listens.
"""
# demucs.py imports torch only inside its functions, so importing it here is cheap and
# the package works without the separation extras until separation is requested.
from . import beatgrid, demucs, percussion, room, stems

__all__ = ['beatgrid', 'demucs', 'percussion', 'room', 'stems']
