# SPDX-License-Identifier: GPL-3.0-or-later
"""Generative models for ``method = "generated"`` sets (optional extras).

Only ``acestep`` (ACE-Step 1.5) is used, by orchestral-dawn. It runs the model in a
separate checkout and venv (see ``requirements-genai.txt``) and caches generations,
so building a set from a cached generation needs no GPU and no ACE-Step install.
"""
from . import acestep

__all__ = ['acestep']
