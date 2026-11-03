"""Raw engine probability summaries and the standard adjusted trajectory score.

Raw area/mean and retained curves are never rewritten. The standard c=2 score
uses a fixed horizon and an explicitly outcome-derived terminal tail, including
predicted probability-stop victories; that tail is not observed model history.
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


# Report/tooling identity, independent of the engine's prediction model and sim version.
SCORE_METRIC = 'log_time_outcome_v1'
SCORE_COEFFICIENT = 2
SCORE_DEFINITION = {
    'metric': SCORE_METRIC, 'coefficient': SCORE_COEFFICIENT,
    'horizon': 'resolved tick limit, otherwise the game job tick limit',
    'terminal_credit': '1 for every declared winner, including shared and probability-stop wins; 0 for losses',
    'tick_cap': 'no terminal tail',
}


def score_horizon(result, config=None):
    """Use the actual absolute tick limit; never normalize by finishing time."""
    value = (result.get('resolved') or {}).get('tick_limit')
    if value is None:
        horizons = set()
        for team in result.get('teams', []):
            cached = team.get('win_probability_log_integral')
            if cached is not None:
                if not isinstance(cached, dict):
                    raise ValueError('invalid compact log probability integral')
                if cached.get('horizon_ticks') is not None:
                    if type(cached['horizon_ticks']) is not int or cached['horizon_ticks'] <= 0:
                        raise ValueError('invalid compact score horizon')
                    horizons.add(cached['horizon_ticks'])
        if len(horizons) > 1:
            raise ValueError('inconsistent compact score horizons')
        if horizons:
            value = horizons.pop()
    if value is None and config is not None:
        from .jobs import DEFAULT_GAME_TICKS
        value = config.get('ticks', DEFAULT_GAME_TICKS)
    if value is None:
        return None
    if type(value) is not int or value <= 0:
        raise ValueError('probability score horizon must be a positive integer')
    return value


def log_integral_summary(team, expected_end_tick, horizon):
    """Sufficient compact statistic for the standard score, before dropping history.

    weighted_area is already divided by ln(1+c), so its units are score weight.
    Raw engine area/mean remain untouched. Old compact area alone cannot recover
    this statistic: missing curves are unavailable, never silently linearized.
    """
    import math
    raw = summary(team, expected_end_tick)
    value = {'metric': SCORE_METRIC, 'coefficient': SCORE_COEFFICIENT,
             'horizon_ticks': horizon, 'model': raw['model'] if raw else None,
             'start_tick': raw['start_tick'] if raw else None,
             'end_tick': raw['end_tick'] if raw else None,
             'weighted_area': None, 'available': False, 'unavailable_reason': None}
    def missing(reason):
        return value | {'unavailable_reason': reason}
    if raw is None:
        return missing('missing_probability')
    if not raw['whole_game']:
        return missing('incomplete_or_mixed_history')
    if type(expected_end_tick) is not int or expected_end_tick <= 0:
        return missing('missing_terminal_tick')
    if horizon is None:
        return missing('missing_horizon')
    if expected_end_tick > horizon:
        return missing('horizon_before_end')
    points = team.get('win_probability_history')
    if points is None:
        cached = team.get('win_probability_log_integral')
        if not cached:
            return missing('missing_history')
        if not isinstance(cached, dict):
            raise ValueError('invalid compact log probability integral')
        if cached.get('metric') != SCORE_METRIC or cached.get('coefficient') != SCORE_COEFFICIENT:
            return missing('unsupported_cached_metric')
        for key in ('horizon_ticks', 'model', 'start_tick', 'end_tick'):
            if cached.get(key) != value[key]:
                return missing('cached_coverage_mismatch')
        if not cached.get('available'):
            return missing(cached.get('unavailable_reason') or 'missing_history')
        area = cached.get('weighted_area')
        observed_weight = math.log1p(SCORE_COEFFICIENT * expected_end_tick / horizon) / math.log1p(SCORE_COEFFICIENT)
        if (type(area) not in (int, float) or not math.isfinite(area)
                or not -1e-12 <= area <= observed_weight + 1e-12):
            raise ValueError('invalid compact log probability integral')
    else:
        if not isinstance(points, list) or len(points) < 2:
            raise ValueError('invalid probability history')
        previous = -1
        for point in points:
            if (not isinstance(point, (list, tuple)) or len(point) != 2
                    or any(type(x) is not int for x in point)
                    or not previous < point[0] <= expected_end_tick or not 0 <= point[1] <= 1000):
                raise ValueError('invalid probability history')
            previous = point[0]
        if points[0][0] != 0 or points[-1][0] != expected_end_tick:
            return missing('incomplete_history')
        exact_area = sum((a[1] + b[1]) * (b[0] - a[0]) for a, b in zip(points, points[1:]))
        if (exact_area != team['win_probability']['area_twice_permille_ticks']
                or points[-1][1] != team['win_probability']['permille']):
            raise ValueError('probability history disagrees with its summary')
        tau = horizon / SCORE_COEFFICIENT
        parts = []
        for (start, first), (end, last) in zip(points, points[1:]):
            duration = end - start
            offset = start + tau
            logarithm = math.log1p(duration / offset)
            slope = (last - first) / (1000 * duration)
            parts.append(first / 1000 * logarithm + slope * (duration - offset * logarithm))
        area = math.fsum(parts) / math.log1p(SCORE_COEFFICIENT)
    return value | {'weighted_area': area, 'available': True}


def group_score(teams, result, config=None):
    """Standard c=2 log-time score with actual or probability-stop outcome credit.

    Allies share one trajectory, never a sum. Probability-stop credits are
    explicitly tagged as predictions; tick-cap adjudication creates no tail.
    """
    import math
    if not teams:
        raise ValueError('probability score requires at least one team')
    end = result.get('ticks')
    horizon = score_horizon(result, config)
    # Preserve the legacy alliance/model consistency check for raw predictions.
    group_summary(teams, end)
    values = [log_integral_summary(t, end, horizon) for t in teams]
    first = values[0]
    score = first | {'score': None, 'outcome_credit': None, 'outcome_source': None,
                     'observed_weight': None, 'terminal_weight': None}
    if any(not v['available'] for v in values):
        unavailable = next(v for v in values if not v['available'])
        return score | {'available': False, 'unavailable_reason': unavailable['unavailable_reason']}
    if any(v != first for v in values[1:]):
        raise ValueError('inconsistent alliance log probability integrals')
    observed_weight = math.log1p(SCORE_COEFFICIENT * end / horizon) / math.log1p(SCORE_COEFFICIENT)
    tail = math.log1p(SCORE_COEFFICIENT * (horizon - end) / (horizon + SCORE_COEFFICIENT * end)) / math.log1p(SCORE_COEFFICIENT)
    termination = result.get('termination')
    credit, source = None, None
    if termination in ('engine_end', 'win_probability'):
        winners = set(result.get('winning_teams', [])) | {t['team'] for t in result.get('teams', []) if t.get('outcome') == 'won'}
        if winners:
            credit = float(any(t['team'] in winners for t in teams))
        elif all(t.get('outcome') == 'lost' for t in teams):
            credit = 0.0
        else:
            return score | {'available': False, 'unavailable_reason': 'missing_terminal_outcome'}
        source = 'probability_stop' if termination == 'win_probability' else 'engine'
    elif termination == 'tick_cap' and end == horizon:
        source = 'tick_cap'
    else:
        return score | {'available': False, 'unavailable_reason': 'unresolved_before_horizon'}
    result_score = first['weighted_area'] + (credit or 0) * tail
    if not math.isfinite(result_score) or not -1e-12 <= result_score <= 1 + 1e-12:
        raise ValueError('invalid adjusted probability score')
    return score | {'score': min(1.0, max(0.0, result_score)), 'outcome_credit': credit,
                    'outcome_source': source, 'observed_weight': observed_weight,
                    'terminal_weight': tail, 'available': True}
