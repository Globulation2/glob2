"""Predeclared finite-look confirmation using simultaneous exact intervals.

Each look uses the existing exact paired interval with its own alpha allocation.
The union bound across looks preserves the hypothesis budget, without requiring
independence between looks. The existing 53-hypothesis allocation is unchanged.
Only complete, ordered scenario prefixes may be examined. No futility stopping.
"""
import math

import maxima_win_statistics as stats


LOOKS = (1000, 2000, 4000, 8000, 14000, 21000)
WEIGHTS = (.02, .03, .05, .10, .20, .60)


def schedule(alpha):
    if not 0 < alpha < 1:
        raise ValueError('invalid hypothesis alpha')
    return [{'pairs': n, 'alpha': alpha * w, 'weight': w}
            for n, w in zip(LOOKS, WEIGHTS)]


def validate_schedule(looks, alpha, maximum):
    ns = [r['pairs'] for r in looks]
    if not ns or ns != sorted(set(ns)) or ns[-1] != maximum:
        raise ValueError('looks must be increasing and end at the maximum')
    if any(type(n) is not int or n <= 0 or n % 500 for n in ns):
        raise ValueError('looks must align with complete 500-pair batches')
    if any(not 0 < r['alpha'] < 1 for r in looks):
        raise ValueError('invalid look alpha')
    if math.fsum(r['alpha'] for r in looks) > alpha * (1 + 1e-12):
        raise ValueError('looks exceed the hypothesis error budget')


def evaluate(pairs, looks, alpha, maximum):
    validate_schedule(looks, alpha, maximum)
    look = next((r for r in looks if r['pairs'] == len(pairs)), None)
    if look is None:
        raise ValueError('inference requested outside a scheduled look')
    result = stats.analyze(pairs, look['alpha'])
    if result['missing_pairs']:
        raise ValueError('scheduled look requires all outcomes and repeat audits')
    significant = result['direction'] is not None
    return {'overall': result, 'look_pairs': len(pairs),
            'hypothesis_alpha': alpha, 'look_alpha': look['alpha'],
            'stop': significant or len(pairs) == maximum,
            'reason': ('adjusted_significance' if significant else 'maximum_sample'
                       if len(pairs) == maximum else 'continue'),
            'interval_scope': 'simultaneous across predeclared looks by union bound',
            'effect_estimate_note': 'Raw estimate at stopping can exaggerate magnitude; use adjusted interval.',
            'default_change_permitted': False, 'combination_check_required': True}
