#!/usr/bin/env python3
"""Check that each control moves the metric it promises, in the promised direction.

The control study reports what moved. This states, per control, what *should* move and which way,
then reads the ablation rows back and passes or fails each claim. A control whose metric does not
follow it is either a dead slider or a mislabelled one; both are bugs.

  python3 control_expectations.py /tmp/study/marchland marchland
"""
import collections, json, math, os, re, statistics as st, subprocess, sys

# control -> (metric, expected sign, human claim). Sign +1 means "more of the control, more of the
# metric"; -1 the other way.
EXPECT = {
    'marchland': [
        ('prizes', 'tel:marchland.rope.prizes-strung', +1, 'more prizes asked for, more strung'),
        ('march', 'tel:marchland.march.tiles', +1, 'a wider march is more no-man\'s land'),
        ('march', 'tel:marchland.homeland.largest-tiles', -1, 'a wider march leaves smaller homelands'),
        ('lakes', 'terrain%:water', +1, 'more lakes is more water'),
        ('levelling', 'tel:marchland.rope.share-solved', -1, 'levelling evens the prizes\' walk'),
        ('wheat-amount', 'tiles:wheat', +1, 'the wheat slider moves wheat'),
        ('wood-amount', 'tiles:wood', +1, 'the wood slider moves wood'),
        ('stone-amount', 'tiles:stone', +1, 'the stone slider moves stone'),
        ('fruit-amount', 'tiles:cherry', +1, 'the fruit slider moves fruit'),
    ],
    'even-ground': [
        ('water-share', 'terrain%:water', +1, 'more water asked for, more water'),
        ('passes', 'tel:even-ground.shape.pass-width-cells', -1, 'tighter passes are narrower'),
        ('balance', 'tel:even-ground.stock.spread-after', -1, 'balance evens the catchments'),
        ('balance', 'tel:even-ground.stock.proposed', +1, 'balance buys search'),
        ('effort', 'tel:even-ground.shape.proposed', +1, 'more effort is more proposals'),
        ('effort', 'tel:even-ground.shape.cost-after', -1, 'more effort ends cheaper'),
        ('wheat-amount', 'tiles:wheat', +1, 'the wheat slider moves wheat'),
        ('wood-amount', 'tiles:wood', +1, 'the wood slider moves wood'),
        ('stone-amount', 'tiles:stone', +1, 'the stone slider moves stone'),
        ('algae-amount', 'tiles:algae', +1, 'the algae slider moves algae'),
    ],
}


def spearman(xs, ys):
    def rank(vs):
        order = sorted(range(len(vs)), key=lambda i: vs[i])
        r = [0.0] * len(vs)
        i = 0
        while i < len(order):
            j = i
            while j + 1 < len(order) and vs[order[j + 1]] == vs[order[i]]:
                j += 1
            for k in range(i, j + 1):
                r[order[k]] = (i + j) / 2.0
            i = j + 1
        return r
    rx, ry = rank(xs), rank(ys)
    mx, my = st.mean(rx), st.mean(ry)
    sx = math.sqrt(sum((v - mx) ** 2 for v in rx))
    sy = math.sqrt(sum((v - my) ** 2 for v in ry))
    return sum((a - mx) * (b - my) for a, b in zip(rx, ry)) / (sx * sy) if sx and sy else 0.0


def main():
    out, generator = sys.argv[1], sys.argv[2]
    rows = [json.loads(l) for l in open(os.path.join(out, 'ablation.jsonl'))]
    square = [r for r in rows if r['w'] == 256 and r['h'] == 256]
    base = [r for r in square if r['study'] == 'baseline' and r['ok']]
    # The study leaves a control out of the request at its default, so the default's maps are the
    # baseline rows. Without folding them back in, every series is missing its default value - which
    # silently drops a three-value control to two and reads as no data at all.
    binary = sys.argv[3] if len(sys.argv) > 3 else 'build/src/glob2'
    listing = subprocess.run([binary, '--list-map-generators', generator],
                             capture_output=True, text=True).stdout
    defaults = {}
    for line in listing.splitlines():
        m = re.match(r'\s+([\w-]+)=(-?\d+)\s+values: ', line)
        if m:
            defaults[m.group(1)] = int(m.group(2))

    print(f'# {generator}: do the controls do what they say?\n')
    ok_all = True
    for control, metric, want, claim in EXPECT[generator]:
        by = collections.defaultdict(list)
        for r in square:
            if r['study'] == 'ablation' and r['control'] == control and r['ok']:
                by[r['value']].append(r)
        by[defaults[control]] = base
        values = sorted(by)
        if len(values) < 3:
            print(f'{control:14s} {metric:44s} NO DATA')
            ok_all = False
            continue
        means, per = [], []
        for v in values:
            got = [r['m'].get(metric) for r in by[v] if metric in r['m']]
            if not got:
                means.append(None)
                continue
            means.append(st.mean(got))
            per.append((v, st.mean(got)))
        if len(per) < 3:
            print(f'{control:14s} {metric:44s} METRIC MISSING')
            ok_all = False
            continue
        rho = spearman([v for v, _ in per], [m for _, m in per])
        lo, hi = per[0][1], per[-1][1]
        flat = abs(hi - lo) < 1e-9
        good = (not flat) and (rho * want > 0.55)
        ok_all &= good
        verdict = 'ok  ' if good else ('FLAT' if flat else 'FAIL')
        print(f'{verdict} {control:14s} {metric:42s} rho={rho:+.2f} '
              f'{per[0][0]}:{lo:.1f} -> {per[-1][0]}:{hi:.1f}   {claim}')
    print('\nALL CLAIMS HOLD' if ok_all else '\nSOME CLAIMS FAILED')


if __name__ == '__main__':
    main()
