# Shared amphibious gradients: final PR verification

Source `c5098d44fe110afd62783f31470a3cc877c792f6`, integrated base `df279a44a19124ca232fbdba32c9a1e6d2688a51`. Save202, replay minimum202, sim126, durable save floor58, network75.

**1004 native cases passed; 15 display cases skipped; no failures.** All affected suites and added terrain/route/animation integration checks were rerun after the final merge. Optimized release verification at1and4participants matches all842committed checksums. Release SHA256 `af5a772f5c3d33f29702906aecc46cf3daac37c360205f6794b6eaa4f22c5d5d`.

Independent subagent review findings were addressed: generation-cost retirement, unused experiment instrumentation, duplicate state, integer APIs, fixture assumptions and typed wrappers. A second review verified incoming terrain/save behavior was preserved. Policy comments and documentation describe selection and publication continuity. Adaptive weights unchanged. No new performance claim for the final architecture.

Linux x86_64 GCC15.2 locally. Non-Linux execution, GPU hardware and manual gameplay remain unverified here; PR hosted checks report separately. Skipped display cases are explicitly listed in validation.json.

Download/extract validation.tar.gz for exact commands, build/test logs and JUnit, review receipt, source patch, dependency hashes, actual format202save/runtime/continuation evidence and1/4participant verification replays/checksums. Build entry points are tools/dev_build.py -j24 engine-tests unit-tests and tools/dev_build.py dev_fast=0 release=1 linker=default -j24, with a verified native SDL prefix and OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1. Commands and logs identify the tested binaries; an evidence archive is not a shipping package.

Earlier format201/prior-base evidence is retained but superseded by this package. Generated evidence lives on this dedicated branch, not in production. No further campaign or schedule was started.
