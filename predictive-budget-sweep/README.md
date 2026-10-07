# Predictive building-gradient budget sweep

Read [the analysis](primary-report.md), view [the percentile/delay/worker curves](primary-summary.png) and [CPU-versus-TPS scatter](primary-summary-tradeoffs.png). This evidence reports a tradeoff curve, not a promotion decision.

Source: [`3fb7141d5152fc2544bd307f62f4023456df1296`](https://github.com/Globulation2/glob2/commit/3fb7141d5152fc2544bd307f62f4023456df1296), pushed on `codex/building-gradient-budget-sweep`. Greedy reference: `ab143f583b16c4cfdab32c9fc4abaa6200867a07`. The measured source predates newer master integration; later rebase/validation is pending. Existing PRs remain draft.

24 evaluation matches across six families; 18 independent training matches; nine budgets × three publication delays × three worker counts; two balanced repetitions plus bracketing controls. All **5,616 timing records** are retained in the two timing archives. `primary-summary.json` includes all 81 points and per-host/family/match statistics. Each measurement retains the exact command, binary/model/checkpoint hashes, result fields, wall/process CPU and RSS. The models and training aggregates are included.

The supplemental eight late-game matches were **skipped at the user’s request** after checkpoint-loading failures. Their protocol and failure logs are retained as excluded evidence. There are no successful supplemental late-game timings in these results. Impact audits, causal hiring/delivery delays and uninterrupted warm-game confirmation are not established by this sweep.

## Inspect or reproduce the analysis

Extract `mac-timing-records.tar.gz` and `linux-timing-records.tar.gz` into this directory. They restore `mac/` and `linux/` in the layout expected by the scripts. Then:

```sh
python3 analyze-sweep.py mac linux --output reproduced-summary.json
python3 -m venv plot-runtime
plot-runtime/bin/python -m pip install -r plot-runtime-requirements.txt
plot-runtime/bin/python plot-sweep.py reproduced-summary.json --title 'Building-gradient sweep: 24 matches across six scenario families'
```

The checkpoint archives restore original greedy saves and generated maps for all primary preparation matches. Existing repository fixtures supply Oazis. Scripts and exact commands record the original artifact paths; update paths to your checkout when rerunning engines. Training demand aggregation inputs are in `training-aggregates.tar.gz`. Check binary hashes before reusing timings; compiled binaries and the multi-gigabyte detailed demand/per-tick traces are not uploaded here.

## Verification and limits

Native macOS ARM64: Apple clang 21, release `-O3`, C++20; native Linux x86_64: GCC 13.3, release `-O3`, C++20. Focused unit cases passed on both platforms, integration and golden logs are retained. 30 checksum runs per host span delays 2/4/8, workers 0/1/2/4/8 and budgets 0/300; whole checksum sidecar hashes match. Cross-platform and continuation manifests are compressed under `validation/`, along with the summary. Raw checksum sidecars are retained locally, not included here. The army fixture failure reproduces on the original greedy source (`base-army.log`); it is not represented as a pass. Other platforms and integration onto newer master remain unverified.

Timing instrumentation and impact auditing were off. Two repetitions do not add independent seeds; intervals resample matches. Loaded-save windows contain 2,048 ticks including cache cold start. Hosts ran different seeds. The report documents outcome counts, histogram limitations and selection uncertainty.

`MANIFEST.json` inventories this evidence snapshot by SHA-256. Earlier evidence on this branch concerns different implementations and must not be pooled with these data.
