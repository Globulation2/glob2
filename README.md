# Cached hiring study evidence

This branch contains review evidence only. Implementation: `codex/cached-hiring-study`.

- `findings.json`: final paired timing, ordinary-workload results, shadow coverage/agreement and limitations.
- `callgrind-findings.json`: instruction attribution, supplier audit and isolated profiling controls.
- `final-test-results.xml`: 128 passing focused engine tests before master integration.
- `provenance.json`: tested revisions, source correspondence, hashes and integration limitations.
- `measurements-manifest.json`: per-file checksums for the measurement/validation archive.
- `callgrind-evidence.tar.gz`: raw profiles, annotations, commands, packages and paired profiling-control runs.

Download both measurement archive parts into one directory, then reconstruct and extract:

```sh
cat measurements-and-validation.tar.gz.part01 measurements-and-validation.tar.gz.part02 > measurements-and-validation.tar.gz
shasum -a 256 measurements-and-validation.tar.gz
tar -xzf measurements-and-validation.tar.gz
```

Compare the archive hash with `provenance.json`. The archive includes exact commands,
raw repetitions, inputs, traces, replay files, saves, logs and metadata. Identical
files are represented by tar hard links. Executable hashes are retained; executables
are available locally rather than uploaded. Historical performance measurements
apply to the source tree committed as `59ce0a239`, before integrating newer master
changes. These results do not establish live hiring gains, gameplay delay or final
cross-platform compatibility. Original hiring remains authoritative.

## Master-integrated head

`integration-provenance.json`, `integration-test-results.xml` and
`integration-validation.tar.gz` retain the refreshed release build, 152 passing
tests and 24 scheduled plus 18 disabled verification runs at the integrated PR
head. `integration-manifest.json` hashes every result file. Large checksum traces
are omitted from this second archive; their bytes match traces already retained
in the measurement archive, as established by the verification records.
Historical timing ratios were not rerun after integrating master.
`callgrind-original-sources.tar.gz` retains original planner/owner sources from
the profiling container, before its isolated controls.

## Demand-driven refresh update

`demand-findings.json` records results at implementation commit `10a52665c`: rank
only buildings with observed staffing vacancies, including route/supplier capture
skips and defense-only attack capture. `demand-validation.tar.gz` retains exact
commands, ten paired repetitions in two distinct windows, shadow outputs, build
logs, 154 passing tests and 42 verification runs. `demand-manifest.json` hashes
all retained results. Large checksum traces match the historical archives and
are represented here by their hashes. This optimization reduces immediate
reference-trajectory coverage to 41–42%; seven-tick controlled wakeup is not a
live gameplay p95 result. Original hiring remains authoritative.
