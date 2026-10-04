# SPDX-License-Identifier: GPL-3.0-or-later
"""Renderers that turn performed scores into audio stems (``method = "composed"``).

A composed set writes its music with ``glob2music.score`` and hands the performed
parts to a backend, which returns one stereo stem per part and mood on the shared
timeline. ``score.pipeline.build_trio`` then mixes the stems into the three moods.
A backend is any object with ``instruments``, ``preroll_s`` and
``render(performed, loop_seconds, work_dir) -> {mood: {part: stem}}``.

* ``sfizz``  -- acoustic instruments from the CC0 VSCO 2 CE and VCSL sample libraries,
  pinned per file in each set's ``samples.lock`` and rendered with sfizz.
* ``surge``  -- synthesiser patches designed in Surge XT (GPL-3.0) from its CC0 Init
  patch, hosted headlessly through pedalboard.
* ``dsp``    -- small synthesised voices (kalimba, droplets, hand percussion) written
  in NumPy for sounds no patch covers.
"""
