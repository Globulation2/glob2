# Permanent wheat comparison

Baseline: origin/master c464848726379f9f286c43452820919727e04078 (slow maturity).
Candidate: same base plus permanent wheat protection; no maturity sampling or toggles, expansion support/wood reserve/layout retained. Source patch and binary hashes accompany the results.

128 matched starts: 16 map families, retained map seeds 6101–6104 transformed by generator, two rotations. `cases.json` records exact game seeds, map hashes, sizes and host assignments. Primarily 128×128; retained Rice cases use 256×256. Fixed opponent Nicowar. Each pair runs on one host with deterministic randomized variant order. therig has 32 concurrent pair workers, devlaptop 16. Each game capped at 32768 ticks. Every command, result and process CPU time is retained remotely in games/<case>/<variant>.

Food/population measures use the common observed time window for each pair; no extrapolation beyond early endings. CPU is process user+system time divided by full-run ticks, and ratios are summarized geometrically. Food is pooled matched-window totals. Confidence intervals cluster-bootstrap the 16 map families with all their seeds/rotations together, 5000 draws. These retained benchmark maps are exploratory, not independent holdouts; the run length does not establish indefinite late-game stability or competitive win rates.

Separate crowded-save CPU test: original farm-crowding.game, ticks 575328–583520; four repetitions per variant, cores 0/2/4/6 on therig. Every physical core runs both versions in seeded randomized order after tournament jobs finish. Same telemetry in both variants. Mean process CPU is reported; simulation trajectories can diverge. The initial devlaptop CPU repetitions overlapped an unrelated compilation and were excluded. Devlaptop tournament process CPU is also potentially noisy from that build; deterministic game outcomes are unaffected.

macOS runs native regressions and checksum/save-continuation validation; Linux validates the exact candidate binary before benchmarking. All versions 115/116/117 saves are loaded, resaved as118 and resumed. macOS/Linux comparison uses per-tick team/entity records for both fresh games and retained saves. Windows is not verified. No system app replacement.
