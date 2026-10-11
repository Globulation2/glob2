# HD loading A+B verification evidence

Implementation PR: https://github.com/Globulation2/glob2/pull/1111

The maintainer approved retaining A+B after cleanup and requested immediate merge, with no further experiments. The original 5% memory gate was explicitly replaced by any measured memory saving without a CPU regression. The remaining browser build and performance queue were stopped on that instruction. This evidence records completed work and coverage limits; it does not claim completion of the original full campaign.

## Tested revisions and environment

Independent four-variant trial base: `16842835bed59d85b60f9054470a086807a24d7d`. The common GUI correction is `322daa07f39c6ed8c1b9d88c2cc86156d32322ee`. Each variant's metadata and source.patch freeze its exact input. Earlier unpolled GUI measurements on other bases are excluded.

Cleaned integrated final candidate: `bec4c7c518333869fbbdb8962bc5bdbddcac438c`. Final baseline: `50d4372f0` (master `285a21f510cf68ac2ddd8c0755de1d2320a6852c` plus identical GUI correction and benchmark improvements). Final baseline/candidate metadata record binary hashes. Current master was fetched before merge: `a6910266e` adds unrelated iOS simulator tooling.

Linux x86_64, kernel 7.0.0, GCC 15.2.0, Ryzen Threadripper 2950X, 16 physical/32 logical CPUs, 123 GiB RAM, RTX 2070 SUPER, NVIDIA 580.178.04. SDL 3.4.16, SDL_image 3.4.6, SDL_ttf 3.2.2, SDL_net 3.2.0, WebP 1.6. See environment.json and dependencies.json for complete inputs. Independent runtime asset manifest SHA256: `743f4a74bd5612b91f4617cdf3364e68e7e148446673b4bde01b27b91c553120` (7,469 files). The newer master changes package metadata; gameplay artwork is unchanged.

Release binaries use `release=1 client_profile=full server=0 CXX=profile-cxx CXXFLAGS=-g1 LINKFLAGS=`. The wrapper removes stripping only, retaining optimized release compilation (-O3, no fast/PCH/unity). Build invocation is preserved in build_final_variants.sh; paths are reproduction inputs, not portable installations. Frozen SDL runtime libraries were used across variants.

## Method and results

84 sequential fresh-process asset benchmark runs: seven alternating baseline/candidate pairs for each A, B and A+B, separately for startup and HD-toggle reload. No competing benchmark games/builds ran. Isolated profiles, fixed rendering/artwork/workers, consistent warm filesystem-cache condition; no machine-wide cache flush. Fingerprints occur outside timing windows. Each result.json/command.json/stdout.log records exact commands and measured process CPU, ready time, kernel peak RSS, sampled residency, texture counters and sampled VRAM.

Eight physical cores were reserved with all SMT siblings (0–7 and 16–23); test engine affinity was 0–7. Ordinary work was kept outside that exclusive cpuset. The asset, heap and final native audits report successful restoration; no core reservations remain. Schedutil was unchanged. CPU reservation cannot isolate memory bandwidth, interrupts, package power or shared caches.

| Independent A+B measurement | Baseline | A+B | Change |
| --- | ---: | ---: | ---: |
| Startup peak live heap, separate trace | 590.93 MiB | 572.07 MiB | -3.19% |
| Reload peak live heap, separate trace | 849.33 MiB | 724.42 MiB | -14.71% |
| Startup allocation count, separate trace | 3,036,880 | 2,764,015 | -8.98% |
| Startup median kernel peak RSS | 699.46 MiB | 697.45 MiB | -0.29% |
| Reload median kernel peak RSS | 933.46 MiB | 909.46 MiB | -2.57% |
| Startup median loading CPU | 10,359.3 ms | 9,930.9 ms | paired geometric change -4.04% |
| Reload median loading CPU | 9,768.55 ms | 9,420.44 ms | paired geometric change -3.43% |
| Startup median ready time | 6,575.09 ms | 6,231.66 ms | paired geometric change -5.17% |
| Reload median ready time | 7,561.91 ms | 7,300.82 ms | paired geometric change -4.01% |
| Steady rendering CPU / frame p95/p99 / completed GPU time | Not remeasured | Not remeasured | No claim |
| Whole-match peak RSS / per-tick simulation equivalence | Not remeasured | Not remeasured | No claim |

Heap figures are one instrumented run per variant/scenario, separate from the seven paired performance runs; instrumented timings are not CPU evidence. Kernel RSS is noisy and includes allocator/driver residency. Ratios above intentionally distinguish median endpoints from paired geometric timing changes.

Independent A startup peak live heap decreased 3.20%; B reload peak live heap decreased 14.70%. All six A/B/AB CPU and ready-time comparisons have one-sided 95% paired log-ratio Student-t upper bounds below the +2% tolerance. For A+B the startup CPU/time upper bounds are -3.35%/-4.05%; reload -2.50%/-2.48%. These are measurement tolerances, not proof of exactly identical speed. B-only reload ready-time upper bound is +1.985%, close to the tolerance. Detailed statistics are in asset-comparison.json. No regression was observed in these loading CPU measurements.

All 84 processes produced HD fingerprint `508276fc863b73cb`, HD CPU pixels 491,478,528 bytes and final texture allocation 388,345,968 bytes. No steady texture-byte reduction is claimed: B removes transient allocation/replacement work. Optional asset failures were expected and consistent. Sampled VRAM includes driver/server noise and does not establish a dedicated VRAM saving.

At A+B reload's global heap peak the largest allocation origins are decoded pixels (including adopted drawable pixels) 513.69 MiB, WebP scratch 128.74 MiB, native sprite preparation/copies 36.53 MiB, texture initialization/opaque driver 26.41 MiB, and opaque driver/library 14.75 MiB. These are allocation origins at one loading peak, not a whole-game ownership census. heap-breakdown.json and per-trace peak.stacks/summary.txt permit inspection; opaque driver stacks are not confidently classified.

## Correctness and final cleanup verification

Independent variants each passed 30 unit and 5 engine cases: 140 total, no failures/skips. Tests cover asset loading, image assets, exclusive/shared adoption, repeated paths, cancellation, partial/missing packs, invalid atlases, successive HD toggles, CPU pixels, sprite sheets, native OpenGL, portable SDL/software, HD integration and unit HD cache. The automatic GUI loop's rendering-thread 2 ms polling is checked in threaded and unthreaded native modes.

After cleanup and integration with master, baseline passed 32 unit + 5 engine cases and candidate passed 34 unit + 5 engine cases: **76 cases, zero failures/errors/skips**. Final logs/XML and native capture PNGs are attached. These final checks cover master's GPU texture preparation and preserve portable base-only inputs. Test durations under correctness conditions are not performance comparisons.

Exact final commands are in final_native_check.sh. In particular:

```sh
bash artifacts/memory-wins/final_native_check.sh final-baseline
python3 test/run_with_benchmark_cpuset.py --help
bash artifacts/memory-wins/final_native_check.sh final-ab
```

The actual final candidate cpuset wrapper command/options are recorded in cpuset-final-native.json. Performance and heap commands were `python3 artifacts/memory-wins/asset_matrix.py` and `python3 artifacts/memory-wins/heap_matrix.py` inside the maintained reservation wrapper; each audit and per-process command.json preserve exact invocation/options. Published scripts require the recorded assets, binary locations and dependency prefix to reproduce.

Cleanup consolidates initial/reload input ownership in an internal helper, documents last-use transfer and distinct preparation modes, keeps atlas staging CPU-only through validation, and documents direct supplied-mip storage without changing general padding. The GUI correction remains a separate commit. No experimental runtime switches or profiling dumps enter master. No simulation/save/replay/network interfaces change.

## Coverage limits accepted at merge

The exact cleaned revision has native correctness checks, but the planned final paired timing rerun was not started. The completed timing trial predates integration with master's GPU texture preparation; its measured percentages are not a timing assertion for bec4c7c51. Current browser release compilation was stopped before completion, and the final serial/threaded browser execution matrix is unverified. Earlier browser experiments used a different base and are not current-platform evidence. Windows/macOS/mobile execution is unverified.

The original fixed/pan/zoom rendering campaign and three full paired 90,000-tick GUI runs per seed (42, 713, 2026, 256x256 Lava Shield, eight Maxima teams and passive observer) were not completed for this candidate. No final whole-match +2% RSS bound, p99 frame bound, completed GPU timing or cross-platform/per-tick checksum equivalence is claimed. Earlier full-game data with unpolled assets are a separate condition, excluded here. The maintainer explicitly requested merge and halt further work with these limits recorded.

Hosted PR checks are cheap policy/contracts only; full hosted platform verification was not requested or completed before merge. The normal master workflow remains responsible for asynchronous full-matrix feedback.
