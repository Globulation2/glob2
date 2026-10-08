# Timer and comparison boundaries

Primary engine throughput uses `run_ns`: starts immediately before engine session preparation, includes fixed 1024 advancing ticks, session shutdown, finishing pending gradients/growth, and diagnostics completion. It excludes loading, initial checksum, final save, and final checksum. Warmup is a separate complete invocation; `--benchmark-warmup 0` during each invocation.

All-thread CPU uses CLOCK_PROCESS_CPUTIME_ID (`benchmark_run_cpu_ns`), starting after session preparation and ending after shutdown and pending computation drain. It excludes setup, saving, and final diagnostics finish. Process wall/CPU and peak RSS from the external runner are also retained; these include setup/teardown and are not interchangeable with engine-only timers.

Tick durations cover stepSession + drawSession on iterations that advance a simulation tick, excluding final drain. Branch builds expose exact median/p95/p99. Untouched master exposes only a logarithmic histogram; report percentile bounds rather than exact numbers. Growth compute, capture, publication, queueing and deadline waits overlap with one another and engine execution; do not sum them into total cost.

Explicit snapshot bytes exclude allocator-internal moves during vector growth. Component copy-call probes instrument only component executables. Their overhead can affect sparse-copy timing; adoption is based on uninstrumented whole-engine executables. Component reset/world construction is outside timed regions.

Master uses immediate growth and different within-pass/random semantics. Identical starting saves and orders do not imply identical trajectories. Compare master-work final material/deposit totals and global growth statistics with every performance claim. Delayed candidates must match baseline checksums and accepted work exactly.

Core isolation reserves four physical cores and their SMT siblings, with benchmark affinity on the four physical logical IDs and performance governors. This prevents unrelated tasks using those cores, but not contention on shared memory bandwidth, last-level cache or package power. Preserve paired order and host-activity records.

Untouched-master snapshot copied-byte totals omit material-stock sidecars counted by this branch; cross-revision raw byte totals are not directly comparable. Within each consistent variant family, the instrumented counters remain useful attribution data.
