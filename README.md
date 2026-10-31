# Evidence for #1268 (8×8 terrain blocks)

Runtime numbers compare master `ddfa81a405`, #1268 as submitted (`211b42e6cd`) and the slimmed layout
(water/deep water on a 4×4 grid, lava/ember field one 8×8 layout; same art as the merged head).
macOS 27.2 arm64 (M3), release builds (`scons release=1`), `torus-render-benchmark --renderer gpu`, 1280×800,
fresh process and profile per run, rotating order, 3 reps. Measurement-only instrumentation is
`instrumentation-lite.patch` (not part of the PR). Scripts: `run-lite.sh`, `analyze.py`.

`summary-lite.txt` columns: load_ms/load_mb at first frame; median/p95/p99 frame ms; over33 = % frames over 33 ms;
missing_pct = % frames with any wanted HD texture not ready; settle_ms = time for HD to arrive after a jump.
`lite_eager` = slimmed with HD deferral disabled (shows why streaming is still needed: +3 s load, +800 MB).

| lava-shield 256 | load s | median ms static / pan1 / pan4 / jump | HD-missing frames pan1 / pan4 / jump |
|---|---|---|---|
| master | 2.5 | 17.5 / 19.1 / 13.7 / 10.6 | 0 |
| #1268 as submitted | 4.0–6.4 | 32.8 / 39.8 / 27.0 / 17.2 | 85% / 61% / 52% |
| merged layout | 3.0–3.5 | 16.7 / 19.7 / 16.5 / 11.9 | 1% / 6% / 2% |

Fast-pan p99: master 45–54 ms, merged layout 66–92 ms (streaming hitch).

## Visuals

`compare-z1.png`, `compare-z2.png`: master / #1268 as submitted / merged layout, GPU path, HD, animation paused,
sea and lava test map (`capture.sh`).

## Download size

`package-size-lite.txt`: release runtime export, before the pack change. Browser before-play (core+game packages)
and total, from `scons/web_assets.plan()` over the release export:

| | before play | browser total |
|---|---|---|
| master | 9.79 MB | 173 MB |
| merged layout, before pack change | 25.03 MB | 204 MB |
| merged head (`847166cc87`+) | 10.39 MB | 189 MB |

## Compiled pack change

`pack/`: the compiled pack only stores border-blended frames. All #1268 materials are periodic, so the pack
is now a 387 KB manifest with no pages (was 14 MB). Terrain preparation timing, same dev binary, swapping
only the pack, interleaved ×5 (old pack verified in use): median 29.5 s old vs 27.1 s new; new faster in all pairs.
The host was heavily loaded (load average 30–60), so only the paired comparison is meaningful.
