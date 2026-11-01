"""Probability summaries from automatic engine statistics, without verbose logs.

The engine integrates piecewise-linear probability with a trapezoidal rule.
Area has units of probability*ticks; mean is area / observed ticks. Never
extrapolate missing history from legacy saves or invent a terminal 100% point.
"""


def stopping_threshold(result, config):
    """Prefer the engine's actual stopping rule to the requested job setting."""
    resolved = (result.get('resolved') or {}).get('rules') or {}
    for fields, key in ((resolved, 'winProbabilityPermille'),
                        (result, 'win_probability_permille'),
                        (config, 'win_probability_permille'),
                        (config.get('rules') or {}, 'winProbabilityPermille')):
        if key in fields:
            return fields[key]
    return None  # Old results may not identify their stopping rule.


def summary(team, expected_end_tick=None):
    """Read one trace; supply the result's terminal tick to check full coverage."""
    value = team.get('win_probability')
    if value is None:
        return None
    if not isinstance(value, dict):
        raise ValueError('invalid win probability summary')
    for name in ('permille', 'start_tick', 'end_tick', 'area_twice_permille_ticks'):
        if type(value.get(name)) is not int:
            raise ValueError('invalid win probability ' + name)
    chance, start, end, area = (value[k] for k in
                               ('permille', 'start_tick', 'end_tick', 'area_twice_permille_ticks'))
    if (not isinstance(value.get('model'), str) or not value['model'] or
            not 0 <= chance <= 1000 or not 0 <= start <= end or not 0 <= area <= 2000 * (end - start)):
        raise ValueError('invalid win probability summary')
    if expected_end_tick is not None and (type(expected_end_tick) is not int or expected_end_tick < 0):
        raise ValueError('invalid win probability terminal tick')
    terminal_covered = expected_end_tick is None or end == expected_end_tick
    return {'model': value['model'], 'final': chance / 1000,
            'start_tick': start, 'end_tick': end, 'covered_ticks': end - start,
            'area': area / 2000, 'mean': area / (2000 * (end - start)) if end > start else None,
            'whole_game': start == 0 and end > start and terminal_covered and value['model'] != 'mixed'}


def group_summary(teams, expected_end_tick=None):
    """Allies share a prediction: count it once, never add their probabilities."""
    values = [summary(t, expected_end_tick) for t in teams]
    if not values:
        raise ValueError('win probability summary requires at least one team')
    if any(v is None for v in values):
        return None
    if any(v != values[0] for v in values[1:]):
        raise ValueError('inconsistent alliance win probabilities')
    return values[0]
