# Fractal generator bulk evidence

These small, checked-in summaries accompany the screenshots and validation report.
They describe the final immutable `service-apron-build` bundle. The raw localhost
map files, previews, telemetry, saves, replays, and exact submitted manifests remain
under the indexed `artifacts/fractal-maps/` directory in the validation workspace.

- `bulk-summary.json` gives the outcome totals, hard/soft finished-world flags,
  and mixed-seed configurations across the four generation cohorts.
- `bulk-jobs.csv` keeps one row per committed generation job, including rejection
  diagnostics and the checks applied to each successful report.
- `default-envelope.csv` classifies every 128/256/512 axis and 2–8-colony default
  combination using all 24 training and held-out seeds. An extra identical baseline
  request per seed is deduplicated; disagreements are reported separately.
- `default-envelope-summary.json` totals those classifications and duplicate checks.

The summaries are derived by `tools.fractal_maps.bulk_inspect` and
`tools.fractal_maps.bulk_support`. A rejected request is not a successful world or
an engine crash. The report explains acceptance limits and visual inspection.
