# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic per-field exploration assignment helpers."""
import hashlib


def intervals(value):
    try:
        result = [int(v) for v in value.split(',')]
    except (ValueError, AttributeError):
        raise ValueError('Exploration intervals must be comma-separated integers') from None
    if not result or len(set(result)) != len(result) or any(v != 0 and not 8 <= v <= 65536 for v in result):
        raise ValueError('Intervals must be unique: 0 (adaptive) or 8..65536 ticks')
    return result


def exploration_seed_for_game(seed, group):
    """Change field assignments across games without using gameplay RNG."""
    return int(hashlib.sha256(f'{seed}:{group}'.encode()).hexdigest()[-8:],16)
