# Simulation memory benchmarks

Compare whole-game memory and CPU with identical retained inputs and optimized binaries.

## Native simulation memory and CPU comparisons

Build the client itself with `scons release=1 server=0`; the `tests` target alone
can leave an older game executable in place. Preserve a baseline with the same
benchmark instrumentation, build options and dependencies before rebuilding.
The structured runner accepts `--benchmark-warmup N` when loading a saved game.
It reports process CPU nanoseconds for setup/loading, execution after the warmup,
and the final save in `result.json`. Setup CPU stops before session startup;
measured execution begins after startup and includes session summary/teardown and
pending pipeline completion. The engine run-wall interval also includes session
startup (and simulation warmup, when requested), so it differs from measured CPU.
Whole-process CPU additionally covers process startup and final teardown; these
phase fields are not an exhaustive partition. Save compression is measured
separately. `--ticks` remains
an absolute game tick, and the warmup must leave a nonempty measured window.

```sh
python3 tools/memory_benchmark.py \
  --baseline artifacts/memory/baseline/glob2 \
  --candidate build/darwin/client/release/src/glob2 \
  --fixture 0=artifacts/memory/initial.game.gz \
  --fixture 15000=artifacts/memory/checkpoint-15000.game.gz \
  --fixture 45000=artifacts/memory/checkpoint-45000.game.gz \
  --output artifacts/memory/comparison
```

Use the appropriate platform build directory. The runner alternates seven pairs,
checks identical final simulation checksums and compressed save bytes, and extends
to at most 21 pairs if the one-sided 95% paired bootstrap upper bound exceeds the
2% CPU regression limit. It retains binary/fixture hashes, commands, raw logs,
timings and one final save per variant and fixture. Keep other heavy work off the
machine. CPU comparisons explicitly disable malloc logging; collect heap profiles
in separate runs using the same fixtures. Record actual capture ticks, distinguish
live allocation totals from resident memory and allocator retention, and report
platforms whose execution could not be checked.

On POSIX, `--interleave-seconds 0.1` alternates the two processes with
`SIGSTOP`/`SIGCONT` within each pair. Process CPU clocks exclude the pauses; this
reduces drift from changing background load without changing the measured tick
window. Both games remain resident, so this is a separate scheduling condition,
not a peak-memory measurement. Retain sequential results too, and identify the
measurement condition when reporting the gate.
Use `--resume` with the same arguments to continue completed pairs after an
interruption. Binary hashes, fixture hashes and measurement settings must match;
the unfinished pair is rerun.
