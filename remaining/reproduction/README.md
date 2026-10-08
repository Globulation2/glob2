# Rebuilding the experiments

Start with a clean archive/checkout of `edb09d40204a1fdb3a6d0e5934ef34e9c344de60` for each variant. The `base/` and `stock-tracking/` overlays preserve the exact source templates recorded in `isolated-source-manifest.json`. Overlay `stock-tracking/` only for C and A+B+C+D; overlay `base/` for control/A/B/D/A+B. These are separate C++ layouts: never mix their object files.

Set each `GROWTH_OPT_A/B/C/D` compile definition to 0 or 1 in the relevant translation units, according to the variant name. `MapResourceState.cpp` uses A/B/C, `ResourceGrowth.cpp` uses B, and `WorldCapture.cpp` uses C/D. All-off is an attribution control; the archived, unmodified executable is the actual baseline. The original per-object compile/link invocations are in `isolated-engine-build-commands.json`; build/test commands and component instrumentation are also preserved. No switches are added to the production CLI.

The stripped all-four production patch is `production-candidate.patch`, applied directly to the clean baseline. `integrated-baseline.patch` applies to master `0f1a2569ab7f23c8702a078978054f73f4ddb9cc` and reconstructs the resolved, unoptimized growth branch integration (tree `5e9f333f8d6bf89c064ad5ac44d95c1b11e41519`). Apply the production patch afterward for the integrated optimized candidate. This preserves master's shared snapshot/session changes.

For the optimized immediate-growth control, use `prepare-growth-optimized-original.py` and `port-growth-common-copy-tests.py` with the archived inputs. It applies only common map/snapshot improvements (A/C/D and the previous contiguous-copy policy) to immediate-growth master. Direct-owner controls change calculation placement only and preserve delayed publication.

Release builds used `CCACHE=1`, `GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix`, the recording dependency prefix recorded in the build logs, and:

```
scons -j12 release=1 server=0 optimized_assets=0 \
  build/linux/client/release/src/glob2 \
  build/linux/client/release/test/glob2-engine-tests
```

The absolute paths in raw commands are historical evidence. Substitute checkout/output/dependency locations when reproducing. Compiler, linked dependency hashes, executable hashes, and fixture hashes are preserved separately. Exact binary hashes are not expected to survive a different toolchain or dependency build.

Use `manifest.json` for the fixed 1,024-tick scenario inputs; starting saves are included with the evidence. `run-growth-remaining.py` runs the initial isolated comparisons; `extend-growth-remaining.py` adds selected repetitions without removing earlier rows. `run-growth-final-pairs.py` consumes the final confirmation/sensitivity plans; `run-growth-final-extension.py` extends explicitly selected comparisons to 30 pairs. The reserved-core/governor wrappers and their audits are included. Complete builds and correctness checks before timing; do not run concurrent builds or tests.

The pilot directories are deliberately excluded from conclusions. Their shared scaffolding changed the all-off control; the corrected overlays above are the measured candidates. The initial `baseline.json` contains historical pilot candidate hashes; `isolated-engine-hashes.json` and each campaign's metadata identify the corrected/final executables.


The final retained source is commit `9e9497d7151effae8fdd00a48db515511d30077a`, integrated with current master `6487b873dd3f29ad3ab75c7513597fa47905c132`. It contains none of the four candidate optimizations. Build these two commits independently with the recorded release flags for `retained-plan.json`; do not apply the experimental overlays. `run-growth-retained-comparison.sh` runs the final fixed-input comparison against current master and the archived edb09d402 baseline. The current-master source archive was made from the exact commit, not the moving branch name.

The final integration resolves master's independent artwork-format144 allocation using combined format146. Correctness checks, real old-save fixture provenance, final golden updates and binary identities are separate from performance results. `retained-freeze.json` identifies the final tested commit/tree and binary; it supersedes the intermediate integrated-baseline tree for delivered-source claims.
