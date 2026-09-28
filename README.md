# Evidence for PR #382: permanent Maxima wheat seeds

Candidate commit: 2d967f6f6ab296b1297136d73a14f0d9b6e3383f.
Baseline master: c464848726379f9f286c43452820919727e04078.

Read REPORT.md for measured results and uncertainty; METHOD.md for the design and limitations. This evidence branch is deliberately separate from maintained source/documentation.

- validation.tar.gz: regression logs, per-tick state hashes, retained 115/116/117 loading/continuation checks, the original crowded 116 save and a 117 prototype save, plus the candidate source patch and reproduction scripts.
- study.tar.gz: exact 128 map inputs with hashes and seeds, runner, parser, analysis/bootstrap scripts and both machines' complete result summaries.
- therig.tar.gz and devlaptop.tar.gz: per-game commands, process CPU, results, telemetry used for measurements and exact computed metrics. Unpack each into a separate directory. The original multi-gigabyte stdout logs were filtered to GLOB2_ECON, GLOB2_MEASURE and GLOB2_PERF_FINAL lines; hashes and byte sizes of the unfiltered originals are included. Recomputing the parser from these archives reproduced all 256 game metrics exactly.
- therig.tar.gz also contains the quiet crowded-save CPU repetitions used in the report. The devlaptop archive's initial compute-results.json is excluded from reporting because an unrelated build overlapped it.
- provenance.json records executable hashes, which matched on both Linux hosts. The baseline source archive is reproducible with `git archive` at the master SHA; candidate sources are the PR commit. Build with `scons -j8 release=1 server=0`.

Results are capped at 32,768 ticks and do not establish indefinite sustainability or competitive win rates. Clustered confidence intervals resample whole map families. Windows checksum parity and human gameplay review remain unverified.
