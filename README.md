# Combined building-gradient / engine-snapshot verification

Source `4639958b1ea60899ef22714b040775e73240668d`, stacked on PR857 at `9d4a364725f1872434403be8ced92e630e529fc7`. PR789 remains draft and its building experiments remain off by default. This is integration verification, not a new performance result or a promotion proposal.

## Integration

Building refreshes now project terrain, resources, occupancy and areas from the Game-owned snapshot store also used by the AI pipeline. Live jobs avoid a second packed Cell array; standalone Map fixtures retain their explicit Cell inputs. Same-tick captures after mutation recapture current components while old leases remain immutable. Resource parents remain consumer-specific immutable inputs. Building delay D and AI delay X remain independent.

AI building access records retain their six semantic route/swim entries; capture maps them from either live storage layout rather than copying the expanded live array. A controlled fixture protects this mapping. Owner-side Maxima policy arbitration now holds its observation through nested helpers. Saved queue admission and load validation use the same snapshot-size definition.

The resource/building executor is still separate from the AI controller executor. This integration shares snapshots, not one physical worker pool. Neither resource publication timing nor AI stream timing was changed to accommodate building deadlines.

Save/replay format141, network protocol60, SIM_REVISION24. Minimum save58 is retained. Released older saves and the AI draft's format140 load; the older standalone gradient draft also used140 with a different private layout and must be regenerated from released checkpoints. The independent draft layouts are not interchangeable. Both pending streams retain deadlines without publishing early on save.

## Final checks

- macOS arm64 / Apple clang21 and Ubuntu24.04 x86_64 / GCC13.3: 331 JUnit cases each, 330 passed, zero failures, one display-only case skipped. Final native build provenance records the clean source revision above. Focused registry includes scheduling, snapshots, all nine controllers, AI state continuation, gradient construction/invalidation, executor failure/slow work, markets, terrain, save/replay/network headers and Maxima economy.
- Three legacy workloads, four building policies, workers0/1/2/4/8: all60 Linux full traces match within their configuration for the 256-tick continuation. All12 corresponding macOS worker4 traces match Linux.
- Building audits on/off: all12 paired full traces match over32 ticks. Experiment off matches the unmodified PR857 control in six paired scenarios: three fresh768-tick runs and three256-tick continuations from AI-format140 saves. Only the aggregate format-version checksum is excluded in this older/newer-format control comparison; every team/entity record is compared.
- Combined streams: seeds19/42/171, AI delays4/8, building delay4, workers0/4,512 ticks. All12 configurations match across macOS/Linux; each worker pair also matches. Native combined tests cover all nine controllers, AI delays0/4/8 × building delays2/4/8, workers0/4, and save/load while both queues are pending.
- Save-phase tests: all28 delay/phase/worker continuations pass on each platform. Compare all full records after the restored boundary, including aggregate checksums. Native tests additionally cover simultaneous deadlines, later dirty state, identity reuse/deletion, new children, partial search continuation, queue fallback and worker failure.
- Golden match verified for SIM24;21 protocol tests, platform typechecks,27 Python gradient-analysis tests and five translation tests pass. Native Studio macOS/Linux traces match the existing digest `3bfb63dd4724281d4c162fd144d137b6da64ed05d35e821511cefb71f28c22fe`,1,849,612 bytes. Browser replay import fixture regenerated at141.

Browser runtimes/Playwright, Windows, Android, display execution and a new ThreadSanitizer pass were not run. These checks do not establish long-run gameplay safety or improved speed. No new ten-repetition timing matrix,10,000-tick population audit or discrepancy-specific causal-fork campaign was performed. The original experiment's performance and 32-seed gameplay results remain historical and require remeasurement on this combined source. Playing the result remains part of review.

## Reproduction and evidence

`mac/` and `linux/` contain exact commands, binary/input/output hashes, complete compressed checksum sidecars, starting checkpoints, save-phase checkpoints and native logs/JUnit. `mac-provenance.json` and `linux/native/linux-857-provenance.json` record source, platform, compiler and build commands; native build-provenance files include flags. `drivers/` contains the selectors and validation scripts. Paths are retained as executed; adapt ignored output directories and SDK paths for another machine. SDK/build dependencies come from this revision's SCons/recording configuration and the configured SDL3 prefixes shown in build commands and logs.

Build through the focused selectors, then run `python3 test/run_tests.py --no-display -j4 --junit <output.xml> --artifacts <output>`. Linux sets `LD_LIBRARY_PATH` to its configured SDL3 prefix. Run the retained drivers from the source root; their manifests preserve every simulation command and seed. Audit runs are separate from timing. The final fixture-only commit did not change the production binaries: hashes in all trace manifests equal the final production hashes.

Previous independent PR851 integration evidence remains at [the earlier verification branch](https://github.com/Globulation2/glob2/tree/codex/building-gradient-851-verification). Historical timing source/report remains [preserved separately](https://github.com/Globulation2/glob2/blob/codex/building-gradient-pre-851-evidence/docs/development/building-gradient-hybrid-results.md).
