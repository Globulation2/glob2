# SPDX-License-Identifier: GPL-3.0-or-later
"""glob2music: build and check Globulation 2 soundtrack sets.

A *set* is three position-aligned, seamlessly looping Ogg Vorbis files (calm,
building, combat) that the game's ``SoundMixer`` crossfades between at the same
playback position. This package turns per-set recipes into such trios and checks
them automatically. Run it from ``tools/music``: ``python3 -m glob2music --help``.

Module map (the shared core; ``score/``, ``backends/``, ``adapt/`` and ``genai/``
add method-specific tools on top of it):

* ``spec``      -- the trio format, mastering targets and every QA threshold;
* ``audio``     -- ``Trio`` type, decoding, frame-exact Vorbis encoding (``write_trio``);
* ``master``    -- loop-aware filters, loudness normalisation, true-peak limiting
  (``finish(trio, spec) -> Trio``);
* ``loop``      -- folding render tails, seam crossfades, loop-point search;
* ``preview``   -- calm -> building -> combat -> calm listening files (``make``);
* ``sources``   -- pinned downloads by URL and SHA-256 (``fetch``);
* ``manifest``  -- ``set.toml``, the recipe contract and ``BuildContext``;
* ``qa``        -- the automatic quality checks (``run_checks``);
* ``cli``       -- ``list | fetch | build | check | preview | install | calibrate``.

The recipe contract: ``sets/<id>/recipe.py`` defines ``build(ctx) -> Trio`` returning
un-mastered float audio; the shared stages master, encode, preview and check it.
"""
from .audio import Trio, read_trio, write_trio
from .spec import DEFAULT_SPEC, MOODS, SAMPLE_RATE, TrioSpec

__all__ = ['DEFAULT_SPEC', 'MOODS', 'SAMPLE_RATE', 'Trio', 'TrioSpec', 'read_trio', 'write_trio']
__version__ = '1.0'
