# SPDX-License-Identifier: GPL-3.0-or-later
"""Recipe: the composition in ``composition.py``, performed by the score layer and rendered
on CC0 samples with sfizz (``samples.lock`` pins every file), mixed with the frozen
``levels.toml``. Mastering is the shared ``master.finish``."""
from glob2music.backends.sfizz import SfizzBackend
from glob2music.score import build_trio, load_composition


def build(ctx):
    return build_trio(ctx, load_composition(ctx.path('composition.py')), SfizzBackend.for_set(ctx))
