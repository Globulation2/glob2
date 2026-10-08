"""Compare closed-form depth formulas with the bucket table, held out by game.

Lifetimes are grouped into fine (serving, hint) cells: eighth-octave buckets of
depth in tiles, plus "missing". A formula's depth is evaluated at a cell's
representative depths, so its owner saving and CPU come from the cell's work
curves exactly as the table's do. Each formula's constants are chosen per lambda
on the training folds by the table's objective (saved - lambda * cpu)."""
import sys, math, itertools, multiprocessing as mp
from collections import defaultdict
sys.path.insert(0, '/home/bradley/glob2-depth-model')
from tools import gradient_depth_fit as F

def fine(depth):
    if depth is None:
        return -1
    x = depth // 10 + 1
    k = x.bit_length() - 1
    return 8 * k + ((x << 3) >> k) - 8

def centre(b):
    """Geometric centre of a fine bucket, in cost units."""
    if b < 0:
        return None
    k, r = divmod(b, 8)
    low = (2 ** k) * (1 + r / 8); high = (2 ** k) * (1 + (r + 1) / 8)
    return max(0.0, (math.sqrt(low * high) - 1) * 10)

games, lives = F.load_dataset(sys.argv[1])
gl = sorted({l.game for l in lives}); fold = {g: F.fold_of(g, F.FOLDS) for g in gl}
accs = defaultdict(F.Acc)
for l in lives:
    accs[(fold[l.game], fine(l.serving), fine(l.hint))].add(l)
del lives
cells = sorted({(s, h) for _, s, h in accs})
index = {c: i for i, c in enumerate(cells)}
held = [[None] * len(cells) for _ in range(F.FOLDS)]
total = [F.Acc() for _ in cells]
for (f, s, h), a in accs.items():
    held[f][index[(s, h)]] = F.Curves.of(a)
    total[index[(s, h)]].merge(a)
total = [F.Curves.of(a) for a in total]
def minus(t, h):
    if h is None: return t
    c = F.Curves(); c.n = t.n - h.n; c.work = t.work - h.work; c.queries = t.queries - h.queries
    for name in ('hits', 'covered', 'saved', 'cpu'):
        setattr(c, name, [a - b for a, b in zip(getattr(t, name), getattr(h, name))])
    return c
train = [[minus(total[i], held[f][i]) for i in range(len(cells))] for f in range(F.FOLDS)]
reps = [(centre(s), centre(h)) for s, h in cells]
print(f'{len(cells)} fine cells', file=sys.stderr)

def k_of(depth):
    return F.grid_index(int(max(F.MIN_DEPTH, min(F.MAX_DEPTH, depth))))

# Formulas: name -> (parameter grid, depth(params, s, h)); s, h in cost units or None.
def ref(s, h):
    return s if s is not None else h
KS = [0.3 * 1.05 ** i for i in range(60)]
def mx(s, h):
    return max(x for x in (s, h) if x is not None)
def mn(s, h):
    return min(x for x in (s, h) if x is not None)
FORMULAS = {
    'a+k*serving':        ([(a, k) for a in range(0, 401, 25) for k in KS],
                           lambda p, s, h: p[0] + p[1] * ref(s, h)),
    'a+k*max(s,h)':       ([(a, k) for a in range(0, 401, 25) for k in KS],
                           lambda p, s, h: p[0] + p[1] * mx(s, h)),
    'a+k*max(s,w*h)':     ([(a, k, w) for a in range(0, 301, 50) for k in KS for w in (0.6, 0.8, 0.9, 1.0, 1.1, 1.25)],
                           lambda p, s, h: p[0] + p[1] * max(x for x in (s, None if h is None else p[2] * h) if x is not None)),
    'a+k*max+c*min':      ([(a, k, c) for a in range(0, 301, 50) for k in KS for c in (-0.2, -0.1, 0, 0.1, 0.2)],
                           lambda p, s, h: p[0] + p[1] * mx(s, h) + p[2] * mn(s, h)),
    'k*max(s,h)^b':       ([(k, b) for b in (0.6, 0.7, 0.8, 0.85, 0.9, 0.95, 1.0, 1.05) for k in [0.3 * 1.06 ** i for i in range(70)]],
                           lambda p, s, h: p[0] * max(mx(s, h), 1.0) ** p[1] * 10 ** (1 - p[1])),
    'a+k*max(s,h)^b':     ([(a, k, b) for a in range(0, 301, 50) for b in (0.8, 0.9, 1.0) for k in [0.3 * 1.06 ** i for i in range(70)]],
                           lambda p, s, h: p[0] + p[1] * max(mx(s, h), 1.0) ** p[2] * 10 ** (1 - p[2])),
}

def fit_and_score(name):
    grid, depth = FORMULAS[name]
    # Precompute grid index per (params, cell) lazily per fold/lambda: depth only depends on params and cell.
    known = [i for i, (s, h) in enumerate(reps) if not (s is None and h is None)]
    missing = [i for i, (s, h) in enumerate(reps) if s is None and h is None]
    ks = {p: [k_of(depth(p, *reps[i])) for i in known] for p in grid}
    rows = []
    for lam in F.LAMBDAS:
        work = saved = cpu = 0.0
        for f in range(F.FOLDS):
            tr = train[f]
            best = max(grid, key=lambda p: sum(tr[i].saved[k] - lam * tr[i].cpu[k] for i, k in zip(known, ks[p])))
            for i, k in zip(known, ks[best]):
                h = held[f][i]
                if h is not None:
                    work += h.work; saved += h.saved[k]; cpu += h.cpu[k]
            for i in missing:  # no history: one constant, as the table's global fallback
                k = F.choose(tr[i], lam)
                h = held[f][i]
                if h is not None:
                    work += h.work; saved += h.saved[k]; cpu += h.cpu[k]
        rows.append({'lambda': lam, 'extra_cpu': cpu / work, 'owner_saving': saved / work, 'params': best})
    return name, rows

with mp.get_context('fork').Pool(len(FORMULAS)) as pool:
    results = pool.map(fit_and_score, FORMULAS)
for name, rows in results:
    scores = [F.under_budget(rows, b)['score'] for b in (1.1, 1.2, 1.25, 1.3)]
    print(name, [(r['lambda'], round(r['extra_cpu'], 4), round(r['owner_saving'], 4), r['params']) for r in rows], file=sys.stderr)
    print(f'{name:16s} saving@1.1={scores[0]:.4f} @1.2={scores[1]:.4f} @1.25={scores[2]:.4f} @1.3={scores[3]:.4f}  '
          f'params@l0.3={next(r["params"] for r in rows if r["lambda"] == 0.3)}')
table = F.cross_validate(F.load_dataset(sys.argv[1])[1])
scores = [F.under_budget(table, b)['score'] for b in (1.1, 1.2, 1.25, 1.3)]
print(f'{"table":16s} saving@1.1={scores[0]:.4f} @1.2={scores[1]:.4f} @1.25={scores[2]:.4f} @1.3={scores[3]:.4f}')
