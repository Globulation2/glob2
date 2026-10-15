# Building material demand — final verification

Tested source: `289584e12e578c033f07dfce080338d5d93c2ae5`.
Base: `9ea290745afb3f6c6fb23f6dabfc52ea20b3d444` (intrusive population traversal, save format 166, sim revision 64).
The working trees and embedded native test provenance are clean. The baseline production source is exactly the base; candidate and baseline use the same compiler, dependencies and release flags.

The final PR retains only a stack-local demand summary and its callers, the independent behavior tests, browser registration and documentation. The summary is 68 bytes, contains only delivery counts, their real-material total and a fullness flag, and never enters saved/snapshot state. One-off measurement tools and unrelated historical fixture repairs are absent from the final PR diff.

## Environment and commands

Linux x86_64, AMD Threadripper 2950X, GCC 13.4, Python 3.14.4. Ordinary release `-O3 -g`, `dev_fast=0 release=1 retain_symbols=1`; no PCH/unity. Existing pinned SDL 3.4.16 / TTF 3.2.2 / image 3.4.6 / net 3.2.0 prefix. Dependencies are unchanged.

`commands.json` records the complete build/test commands, environment, source identities and coverage rationale. Test inventories, JUnit, stdout, embedded build provenance, saves, replays and per-tick traces are included. The one-off scripts are under `review-tools/`; set `GLOB2_REVIEW_ROOT` to the tested checkout. Adjust the recorded dependency prefix when reproducing on another host.

## Results

- Client, engine and unit release builds: passed.
- Focused engine suites: **97 passed**, including all eight new material-demand cases, stock, services, production/combat, fetch apportionment, recruitment ordering, historical/current saves and turn/replay acceptance.
- Focused unit suites: **27 passed**.
- Building catalogs and capabilities: **16 passed**, one display case skipped.
- UBSan on Construction, Step, Update and the demand oracle: **eight cases passed**, including generated states and authored arithmetic bounds. This is targeted instrumentation, not full-engine sanitizer qualification.
- Early eight-Nicowar Playground simulation, seed 713: baseline/candidate match **4,096 tick checksums**, replay bytes and sidecar bytes.
- Mature checkpoint at tick 24,000: baseline/candidate match **1,153 tick checksums** through 25,153, replay bytes and sidecar bytes. The fixed normalization driver completed on this base; every requested endpoint and expected trace length was checked.
- Save/load continuation: another **128 ticks** match uninterrupted execution.
- The mature checkpoint also matches exactly with **one versus eight compute participants**.
- Standard native replay (1,500 ticks), committed multiplayer record, frozen resource compositions (150 rows) and unit ablations (97 rows): passed.
- Documentation: all **309** Markdown documents checked, zero errors. Sim version, RNG ownership and whitespace checks passed. No sim/save/replay/network version change is introduced by this PR.

The CLI smoke suite passed **14/15**. Its renderer Bash-completion assertion expects only `software`, while current production also returns `sdl-gpu` and `sdl`. The same test fails identically against the untouched base binary (`baseline-cli-completion.log`). This unrelated expectation is not changed here.

## Local measurements

Three interleaved processes per variant, seven samples per process, pinned to CPU 31, all correctness digests checked. These are shared-workstation microbenchmarks, not isolated or whole-engine performance qualification. Raw samples, per-run variation, commands and section sizes are in `measurements/`.

For the mixed-population fixture, median public `step` cost was **228.3 → 127.6 ns** and `update` **288.4 → 130.6 ns**. Across the retained sample scenarios, those two callers improved substantially. Some dense stand-alone recruitment-list queries regressed (dense authored: **110.8 → 121.0 ns**, approximately 9%); the implementation keeps one simple calculation rather than adding another specialized path. No frame-rate or portable speedup claim is made.

The three changed production objects gained **1,939 bytes** of `.text` in this compiler configuration (57,335 → 59,274 bytes total). No persistent memory, settings, dependencies or SIMD implementation was added.

## Hosted and omitted coverage

The full hosted run is https://github.com/Globulation2/glob2/actions/runs/38821017434 . Native Linux builds passed the prior stale-cache configuration failure and are compiling; macOS ARM64 qualification is also running. Windows stopped before compilation because the existing gradient shader bundle fails freshness verification. The first browser build restored an obsolete configuration marker; its stale object caches were removed and a same-revision browser-focused run started at https://github.com/Globulation2/glob2/actions/runs/38822698406 . The unrelated platform-stack job also failed in its existing design-system package build. No successful Windows, ARM64 or browser execution is claimed in this evidence.

The PR uses the repository's focused local-verification path. Full hosted CI, full-engine sanitizers, package/store verification, display/play review and isolated whole-engine performance coverage are not claimed. No passing claim relies on a cheap-only CI result or on earlier-base validation.

## Extracting the archive

Identical files (including byte-identical baseline/candidate traces) are stored once. Unzip the archive, then run `python3 restore-evidence.py` inside the extracted directory to restore all original paths and verify every file against `SHA256SUMS.json`. `DUPLICATES.json` maps each omitted copy to its retained canonical bytes.
