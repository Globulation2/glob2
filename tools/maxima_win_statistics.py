#!/usr/bin/env python3
"""Fixed-sample exact ON-minus-OFF tournament-winning-probability inference.

Natural wins or protocol-defined population-cutoff wins; draws are non-wins.
No optional stopping, activation filtering, or repeat weighting.
CP(q) and CP(r | discordances) each receive alpha/2. Their Cartesian image
under q*(2*r-1) covers Delta by the union bound, including zero discordances.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import numpy as np
from scipy.stats import beta


def cp(k, n, alpha):
    if not 0 < alpha < 1:
        raise ValueError('alpha must lie in (0,1)')
    k, n = np.broadcast_arrays(np.asarray(k), np.asarray(n))
    if np.any(k < 0) or np.any(k > n) or np.any(n < 0):
        raise ValueError('invalid binomial counts')
    low = np.where(k == 0, 0., beta.ppf(alpha / 2, k, n - k + 1))
    high = np.where(k == n, 1., beta.ppf(1 - alpha / 2, k + 1, n - k))
    return low, high


def exact_interval(n, on_only, off_only, alpha, missing=0):
    """Outer envelope over *all* completions of missing pairs, within same alpha.

    A partially missing pair is conservatively permitted either discordance or
    agreement. q has d..d+m successes; r has b..b+m among d..d+m discordances.
    Monotonicity of CP endpoints yields this envelope without imputing a draw.
    """
    n, b, c, m = np.broadcast_arrays(n, on_only, off_only, missing)
    if np.any(n <= 0) or np.any(b < 0) or np.any(c < 0) or np.any(m < 0) or np.any(b+c+m > n):
        raise ValueError('invalid paired counts')
    qlo = cp(b+c, n, alpha/2)[0]
    qhi = cp(b+c+m, n, alpha/2)[1]
    rlo = cp(b, b+c+m, alpha/2)[0]
    rhi = cp(b+m, b+c+m, alpha/2)[1]
    corners = np.stack([q*(2*r-1) for q in (qlo,qhi) for r in (rlo,rhi)])
    return corners.min(axis=0), corners.max(axis=0)


def conclusion(low, high):
    direction = 'helpful' if low > 0 else 'harmful' if high < 0 else None
    equivalent = low > -.02 and high < .02
    return {'direction': direction, 'practically_equivalent': equivalent,
            'magnitude_above_two_points': low > .02 or high < -.02,
            'status': direction or ('practically_equivalent' if equivalent else 'unresolved')}


def analyze(pairs, alpha):
    seen = set()
    b = c = missing = on_wins = off_wins = 0
    for pair in pairs:
        if pair.get('repeat', False):
            raise ValueError('repeat audit is not an independent pair')
        key = pair['scenario_id']
        if key in seen:
            raise ValueError('duplicate scenario in one hypothesis')
        seen.add(key)
        on, off = pair['on'], pair['off']
        if any(x is not None and (type(x) not in (int, bool) or x not in (0,1)) for x in (on,off)):
            raise ValueError('outcomes must be binary tournament wins or unresolved null')
        on_wins += on == 1
        off_wins += off == 1
        if on is None or off is None:
            missing += 1
        else:
            b += on == 1 and off == 0
            c += on == 0 and off == 1
    n = len(seen)
    low, high = map(float,exact_interval(n,b,c,alpha,missing))
    return {'independent_pairs': n, 'on_only': b, 'off_only': c, 'missing_pairs': missing,
            'on_observed_wins': on_wins, 'off_observed_wins': off_wins,
            'delta': (b-c)/n if not missing else None,
            'outcome_bounds': [(b-c-missing)/n,(b-c+missing)/n],
            'q': (b+c)/n if not missing else None,
            'r': b/(b+c) if b+c and not missing else None,
            'alpha': alpha, 'ci': [low,high], **conclusion(low,high)}


def simulated_power(n, q, delta, alpha, campaigns=10000, seed=20260906):
    if campaigns < 10000 or not abs(delta) <= q <= 1:
        raise ValueError('require >=10000 simulations and |delta| <= q <= 1')
    rng = np.random.default_rng(seed)
    d = rng.binomial(n,q,campaigns)
    b = rng.binomial(d,(1+delta/q)/2)
    low, high = exact_interval(n,b,d-b,alpha)
    detected = low > 0 if delta > 0 else high < 0
    successes = int(detected.sum())
    lo,hi = cp(successes,campaigns,.01)
    return {'n':n,'q':q,'delta':delta,'campaigns':campaigns,'power':successes/campaigns,
            'power_99_ci':[float(lo),float(hi)],'seed':seed}


def size_from_pilot(pairs, hypotheses, maximum=2000000, campaigns=10000):
    # Pilot is used only for variance/cost. Missing pairs can all disagree.
    pilot = analyze(pairs,.05)
    n=pilot['independent_pairs']; d=pilot['on_only']+pilot['off_only']
    qlow=float(cp(d,n,.01)[0]); qhigh=float(cp(d+pilot['missing_pairs'],n,.01)[1])
    qhigh=max(.02,qhigh)
    grid=np.linspace(max(.02,qlow),qhigh,5)
    alpha=.04/hypotheses
    ntrial=100
    attempts=[]
    while ntrial<=maximum:
        checks=[simulated_power(ntrial,float(q),sign*.02,alpha,campaigns)
                for q in sorted(set(grid)) for sign in (-1,1)]
        attempts.append({'n':ntrial,'minimum_power_lower_bound':min(c['power_99_ci'][0] for c in checks)})
        if attempts[-1]['minimum_power_lower_bound'] >= .90:
            return {'required_pairs':ntrial,'alpha':alpha,'pilot_q_99_bounds':[qlow,qhigh],
                    'power_checks':checks,'attempts':attempts,'allocation_required':True,
                    'pilot_excluded_from_confirmation':True}
        ntrial=int(np.ceil(ntrial*1.5/100)*100)
    return {'required_pairs':None,'status':'exceeds sizing ceiling; allocation unresolved',
            'attempts':attempts,'pilot_q_99_bounds':[qlow,qhigh]}


def sensitivity(hypotheses=53, campaigns=10000, seed=910604):
    if campaigns<10000 or hypotheses<1:
        raise ValueError('at least 10000 campaigns and one hypothesis required')
    rng=np.random.default_rng(seed)
    # Nulls span rare wins/high correlation through maximal disagreement.
    q=np.resize(np.array([0.,.002,.02,.1,.5,1.]),hypotheses)
    n=20000
    d=rng.binomial(n,q,(campaigns,hypotheses)); b=rng.binomial(d,.5)
    low,high=exact_interval(n,b,d-b,.04/hypotheses)
    errors=int(np.any((low>0)|(high<0),axis=1).sum())
    lo,hi=cp(errors,campaigns,.01)
    effects=[simulated_power(n,q,delta,.04/hypotheses,campaigns,seed+1)
             for q in (.02,.1,.5,1.) for delta in (-.02,.02)]
    effects += [simulated_power(n,q,delta,.04/hypotheses,campaigns,seed+2)
                for q in (.1,.5,1.) for delta in (-.08,.08)]
    return {'campaigns':campaigns,'hypotheses':hypotheses,'seed':seed,
            'familywise_errors':errors,'familywise_error':errors/campaigns,
            'familywise_error_99_ci':[float(lo),float(hi)],
            'error_control_passed':float(hi)<=.04,'sensitivity':effects,
            'interpretation':'Synthetic qualification, not evidence about game switches; power varies with q.'}


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sensitivity',action='store_true')
    parser.add_argument('--hypotheses',type=int,default=53)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if not args.sensitivity: parser.error('select --sensitivity')
    args.output.write_text(json.dumps(sensitivity(args.hypotheses),indent=2)+'\n')
