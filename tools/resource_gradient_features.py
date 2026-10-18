# SPDX-License-Identifier: GPL-3.0-or-later
"""Versioned production inputs; the seven-column collection contract stays fixed.

Count transforms clamp at inference, but fractions use the original uint64
counts. Clamping both counts first would change rates for busy, old fields.
"""
from tools.resource_gradient_dataset import FEATURES as BASE_FEATURES
from tools.resource_gradient_dataset import CONTRADICTION_CONTRACT, CONTRADICTION_COUNTERS

CONTRACT_ID = 'goal-stall-v1'
COUNTER_IDS = (0, 4, 5)  # Stable diagnostic CSV order, not model-column order.
COUNTERS = tuple(CONTRADICTION_COUNTERS[i] for i in COUNTER_IDS)
FRACTIONS = tuple(name + '_frac_q12' for name in COUNTERS[:-1])
FEATURES = (*BASE_FEATURES, *COUNTERS, *FRACTIONS)
TRANSFORMS = ('log1p_q12',) * (len(BASE_FEATURES) + len(COUNTERS)) + ('identity_q12',) * len(FRACTIONS)
UINT64_MAX = (1 << 64) - 1
CONTRACT = dict(id=CONTRACT_ID, base_features=list(BASE_FEATURES),
                diagnostic_contract=CONTRADICTION_CONTRACT,
                counter_ids=list(COUNTER_IDS), features=list(FEATURES),
                transforms=list(TRANSFORMS),
                rate='floor(4096*min(uncapped_event_count,uncapped_movement_queries)/max(1,uncapped_movement_queries))')


def q12_fraction(events, queries):
    if any(type(v) is not int or not 0 <= v <= UINT64_MAX for v in (events, queries)):
        raise ValueError('Movement counts must be uint64 integers')
    # Inference is bounded even for corrupt numerical inputs. Dataset loading
    # and prepare_rows still reject event counts above the query denominator.
    return min(events, queries) * 4096 // max(1, queries)


def prepare(base, counters):
    """Selected numerical inputs; derive uncapped rates before clamping counts."""
    if len(base) != len(BASE_FEATURES) or len(counters) != len(COUNTERS):
        raise ValueError('Production input vector dimensions differ from contract')
    goal, stalled, queries = counters
    rates = (q12_fraction(goal, queries), q12_fraction(stalled, queries))
    return [*base, *(min(v, 1_000_000) for v in counters), *rates]


def prepare_rows(rows):
    # Research and production must filter the same population and derive the
    # same rates. This also rejects unknown or incompatible collector contracts.
    from tools.resource_gradient_contradictions import prepare as prepare_research
    result, excluded = prepare_research(rows)
    for row in result:
        vector = prepare([row[f] for f in BASE_FEATURES], [row[f] for f in COUNTERS])
        for name, value in zip(FEATURES, vector):
            if name in FRACTIONS and row[name] != value:
                raise ValueError('Research and production fraction preparation differ')
            if name in FRACTIONS:
                row[name] = value
    return result, excluded
