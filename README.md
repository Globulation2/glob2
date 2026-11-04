# Shared amphibious gradients: final PR verification

Source `f466c5562982507001b790d01da8205f3b9b720f`, integrated base `bc11b2b6770184574c84cff87f78d73306fa306e`. Save203, replay minimum203, sim126, durable save floor58, network75.

**104 native cases passed; 13 display cases skipped; no failures.** These are focused final integration checks. The prior full run passed1004cases with15display skips (1019total) on sourcec5098d44fe/base df279a44a1; its evidence is retained separately. Final master only appends an Ice-rule alias and save-ID gate; the routing implementation and existing terrain behavior are unchanged. Optimized release verification at1and4participants matches all842committed checksums. Release SHA256 `ec73e2235d379e2178f0a290461ca9c2689865e568d182a7fbda6c2fa314bae4`.

Independent subagent review findings were addressed: generation-cost retirement, unused experiment instrumentation, duplicate state, integer APIs, fixture assumptions and typed wrappers. Independent integration reviews verified incoming terrain/save behavior was preserved, including the final additive alias. Policy comments and documentation describe selection and publication continuity. Adaptive weights unchanged. No new performance claim for the final architecture.

Linux x86_64 GCC15.2 locally. Non-Linux execution, GPU hardware and manual gameplay remain unverified here; PR hosted checks report separately. Skipped display cases are explicitly listed in validation.json.

Download/extract validation.tar.gz for exact commands, build/test logs and JUnit, review receipt, source patch, dependency hashes, actual format203save/runtime/continuation evidence and1/4participant verification replays/checksums. Build entry points are tools/dev_build.py -j16 engine-tests unit-tests and tools/dev_build.py dev_fast=0 release=1 linker=default -j16 (final incremental release check uses -j24), with a verified native SDL prefix and OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1. Commands and logs identify the tested binaries; an evidence archive is not a shipping package.

The embedded prior-full-validation.tar.gz records the full prior-base run and must not be mistaken for the final-format203 focused run. Generated evidence lives on this dedicated branch, not in production. No further campaign or schedule was started.
