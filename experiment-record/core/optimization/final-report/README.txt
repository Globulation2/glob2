Final core optimization report (read-only analyzer)

Run from /home/bradley/.codex/worktrees/engine-optimization/glob2:
  taskset -c 15,31 python3 artifacts/engine-optimization/final-report/analyze.py --combined-folder artifacts/engine-optimization/final-combined-performance-v3

Preparation check:
  python3 artifacts/engine-optimization/final-report/analyze.py --plan-only

The script never launches native engines, browsers, builds or profilers. It reads
sealed raw measurements and receipts and writes only this report directory. It
requires exact source, dependency, binary and receipt hashes before promoting
validation. Final fixed-window metrics require all 21 paired jobs. Natural-game
metrics require all 18 attempts, including explicitly reported one-hour censors.
Earlier isolated results are never substituted for incomplete final metrics.

Outputs: analysis.json, summary.html, details.html; final-layout-metrics.svg/png
when the complete fixed-window gate exists and matplotlib is available. The
existing plotting environment may be used; no dependency installation is needed.

All estimates are descriptive. Fixed-window ratios use equal weight per listed
layout, then per seed and paired repeat. Full-game ratios require both members
to complete with exact outcome/replay equality; each pair has one run per variant.
Capped attempts are visible and excluded from completed-game means. Peak RSS
means process high-water, with a fresh-wrapper RSS floor guard. GPU fractions
are completed/requested field counters, not device time or proof of uninterrupted
GPU service. accepted remains false until the maintainer reviews final evidence.

The default combined cohort is final-combined-performance-v3. --combined-folder
selects an explicit sealed cohort; there is no fallback to earlier failed runs.
Original and v2 attempt folders are recorded as ineligible measurement artifacts;
strict native-HWM greater than controller-HWM is required for every accepted row.
For v3, per-game controller-proof helper/execute/floor provenance is also bound.
