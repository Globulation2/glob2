"""Probability summaries from automatic engine statistics, without verbose logs.

The engine integrates piecewise-linear probability with a trapezoidal rule.
Area has units of probability*ticks; mean is area / observed ticks. Never
extrapolate missing history from legacy saves or invent a terminal 100% point.
"""


def summary(team):
    value = team.get('win_probability')
    if value is None:
        return None
    for name in ('permille', 'start_tick', 'end_tick', 'area_twice_permille_ticks'):
        if type(value.get(name)) is not int:
            raise ValueError('invalid win probability ' + name)
    chance, start, end, area = (value[k] for k in
                               ('permille', 'start_tick', 'end_tick', 'area_twice_permille_ticks'))
    if not value.get('model') or not 0 <= chance <= 1000 or not 0 <= start <= end or not 0 <= area <= 2000 * (end - start):
        raise ValueError('invalid win probability summary')
    return {'model': value['model'], 'final': chance / 1000,
            'start_tick': start, 'end_tick': end, 'covered_ticks': end - start,
            'area': area / 2000, 'mean': area / (2000 * (end - start)) if end > start else None,
            'whole_game': start == 0 and end > start and value['model'] != 'mixed'}


def group_summary(teams):
    """Allies share a prediction: count it once, never add their probabilities."""
    values = [summary(t) for t in teams]
    if any(v is None for v in values):
        return None
    if any(v != values[0] for v in values[1:]):
        raise ValueError('inconsistent alliance win probabilities')
    return values[0]
