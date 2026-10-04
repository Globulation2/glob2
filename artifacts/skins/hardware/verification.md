# Hardware skin verification

Tested on the NVIDIA RTX 2070 SUPER at PCI 42:00.0 (GPU 1), driver 580.178.04, Linux x86_64, Ryzen Threadripper 2950X. GPU 0 was detected but not separately benchmarked.

Native helper built with GCC release optimization and the pinned SDL3 dependency prefix. Source/binary hashes and revision metadata are in verification.json and results.json. These tests exercise uncommitted mesh/editor changes.

The helper now prints actual renderer metadata and presents warm-up frames for 500 ms before tests, allowing desktop window animation to finish. Earlier runs without that settling step are superseded. Final benchmark runs are sequential, alternating old/new order across ten runs per workload. Each run draws 512 sprites, warms five measured-scene frames, then measures forty frames. Timing includes preparation, composition, frame caching and presentation; it is not a GPU-only timer. Vblank synchronization was disabled using __GL_SYNC_TO_VBLANK=0. The normal desktop compositor and other existing applications remained running, so results have substantial variation.

| Pose/paint combinations | Path | Old median (ms) | Repaired median (ms) | Repaired range (ms) |
| --- | --- | ---: | ---: | ---: |
| 128 | immediate | 4.211 | 3.864 | 3.659–5.503 |
| 128 | atlas | 2.896 | 2.791 | 2.449–4.747 |
| 512 | immediate | 4.395 | 3.549 | 2.642–7.375 |
| 512 | atlas | 2.807 | 3.066 | 1.892–5.377 |

No meaningful performance gain or regression is established by these noisy isolated measurements. They do establish hardware rendering access and successful execution of both workloads.

Native hardware captures: all seven animated clips, eight headings, 32 phases, four paint patterns, plus unchanged swarm: 228 capture pages. All pages compared against software OpenGL captures, with mean absolute channel error below 0.1/255 on every page. Cache-hit image pairs are pixel-identical. Native cache invalidation/address reuse/overflow and opacity checks passed.

Chromium WebGL2 reports ANGLE on NVIDIA RTX 2070 SUPER (webgl-info.json). All eight existing skin browser tests passed with headed hardware rendering, covering desktop and phone layouts, drafts/publishing, action/frame controls, painting, erasing and undo/redo. This is phone layout emulation, not physical mobile hardware.

Commands:
```sh
GLOB2_SDL3_PREFIX="$PWD/artifacts/skins/native-deps" scons -j6 release=1 skin-preview
python3 artifacts/skins/hardware/run.py
PATH="/home/bradley/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin:$PATH" node artifacts/skins/hardware/webgl-probe.mjs
PATH="/home/bradley/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin:$PATH" platform/node_modules/.bin/playwright test -c artifacts/skins/hardware/playwright.config.ts skins.spec.ts
```

Coverage limits: no physical mobile GPU, no other GPU vendor, no GPU 0 comparison, and no crowded full-game benchmark. Asset download/storage size remains higher.

## Final current-master integration verification

Final PR head: `8a4aecff7c6fea0776af7c2efd5bb001d46ea95e`. Base: `d0a8fe311297c211c6feba870307f0cc22764b88`. Tested merge tree: `5224487a7f2512b51df5b556764bee08c460c0be`. A clean temporary merge was tested and then aborted; the final PR head combined with that base produces exactly the tested tree.

Asset tests: 5 passed (49.636 seconds); native SkinMesh tests: 3 cases passed; platform lint/type checks and web build passed; focused API tests: 3 files / 5 tests passed; hardware Chromium browser tests: 8 desktop/phone-layout tests passed. Rebuilt the native helper and unit harnesses against current master. Hardware cache/opacity checks passed; all 57 white integration capture pages matched references below 0.1/255 mean channel error, and all cache-hit image pairs were pixel-identical. Logs are under artifacts/skins/integration-*.log.

The initial asset run exceeded its old 60-second timeout during concurrent builds on this host. The per-pose checks are unchanged; the bounded time allowance is now 180 seconds. The rerun passed in 49.636 seconds. No simulation, save/load or network behavior was changed, so no simulation/replay compatibility run or revision bump was required. Full game WASM, physical Android/iOS, Windows and macOS coverage were not run for this asset/editor change. Hosted PR checks are cheap contracts only and do not establish this local coverage. The user explicitly approved merging after reviewing the hardware evidence.
