# #537 after rebasing onto master fe33142db (SDL3)

- `parity-off-vs-master.txt`: the 30 SoftwareRenderBenchmark captures of
  `../../parity/run.sh`, master `fe33142db` (twice) vs #537 with Smooth unit
  motion off: 29 byte-identical, 1 differing only inside master's run-to-run
  noise region. Hashes in `parity-sha256.txt`.
- `tsan-headless.log`, `tsan-windowed.log`: debug ThreadSanitizer build (macOS,
  pinned SDL3), the two games of `.github/workflows/thread-sanitizer.yml` with
  `halt_on_error=1`: no reports (windowed with `report_thread_leaks=0`; without it
  TSan reports two threads SDL3 creates at startup and never joins).
