"""Summarize paired master/#1268 benchmark logs (measurement-only)."""
import re, sys, glob, os, statistics, collections
A = os.path.dirname(os.path.abspath(__file__))
def pct(v, p): v = sorted(v); return v[min(len(v)-1, max(0, (len(v)*p+99)//100-1))]
rows = collections.defaultdict(list)
RUNS = sys.argv[1] if len(sys.argv) > 1 else f"{A}/runs"
for path in sorted(glob.glob(f"{RUNS}/*.log")):
    name, sc, mp, rep = os.path.basename(path)[:-4].rsplit('-', 3)[0], *os.path.basename(path)[:-4].split('-')[1:2], None, None
    base = os.path.basename(path)[:-4]
    name, rest = base.split('-', 1); sc, rest = rest.split('-', 1); mp, rep = rest.rsplit('-', 1)
    text = open(path).read()
    load = re.search(r"MEASURE_LOAD ms=([\d.]+) footprint_mb=([\d.]+)", text)
    frames = {int(m[1]): float(m[2]) for m in re.finditer(r"^FRAME mode=2D no clouds frame=(-?\d+) elapsed_ms=([\d.]+)", text, re.M)}
    hd = {int(m[1]): (int(m[2]), int(m[3]), float(m[4])) for m in re.finditer(r"MEASURE_HD frame=(-?\d+) missing_or_pending=(\d+) wanted=(\d+) poll_ms=[\d.]+ footprint_mb=([\d.]+)", text.replace("pending=", "missing_or_pending="))}
    if not load or not frames: rows[(sc, mp, name)].append(None); continue
    measured = [frames[i] for i in sorted(frames) if i >= 0]
    hdm = {i: v for i, v in hd.items() if i >= 0}
    missing_frames = sum(1 for v in hdm.values() if v[0] > 0)
    # Jump settle: cameraFrame = i + 2 jumps when divisible by 120.
    settles = []
    if sc.startswith('jump'):
        for j in range(120, 602, 120):
            start = j - 2 + 1  # first measurement reflecting the new view
            n, ms = 0, 0.0
            for i in range(start, min(start + 119, 600)):
                if i in hdm and hdm[i][0] > 0: n += 1; ms += max(16.667, frames.get(i, 0))
                elif i in hdm: break
            settles.append((n, ms))
    rows[(sc, mp, name)].append(dict(load_ms=float(load[1]), load_mb=float(load[2]),
        peak_mb=max(v[2] for v in hd.values()), median=statistics.median(measured), p95=pct(measured, 95), p99=pct(measured, 99),
        over33=sum(1 for t in measured if t > 33.3) / len(measured) * 100,
        missing_pct=missing_frames / max(1, len(hdm)) * 100, missing_max=max((v[0] for v in hdm.values()), default=0),
        wanted_max=max((v[1] for v in hdm.values()), default=0),
        settle_ms=statistics.median([s[1] for s in settles]) if settles else None,
        settle_max=max((s[1] for s in settles), default=None)))
def med(xs, k):
    v = [x[k] for x in xs if x and x[k] is not None]
    return statistics.median(v) if v else float('nan')
keys = ['load_ms','load_mb','peak_mb','median','p95','p99','over33','missing_pct','missing_max','wanted_max','settle_ms','settle_max']
print("scenario map build n " + " ".join(keys))
for (sc, mp, name), xs in sorted(rows.items()):
    ok = [x for x in xs if x]
    print(sc, mp, name, f"{len(ok)}/{len(xs)}", " ".join(f"{med(ok,k):.1f}" for k in keys))
