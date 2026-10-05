# Runtime terrain review: final material integration

Tested/pushed revision: `ea5407b22fb00bbf223b283bd5c7c78d098f2177`.
Integrated master/base: `d7e3af8ec3e58455a55a55e3ce347df0ec930115`.

[Download evidence](evidence.tar.gz) · [SHA-256](archive.sha256) · [Desktop palette](screenshots/terrain-palette-desktop.png) · [Phone palette](screenshots/terrain-palette-phone.png) · [Custom terrain on phone layout](screenshots/terrain-map-phone.png)

## Review and integration

Two independent reviewers covered simulation/persistence and presentation/editor architecture, implemented findings, and reviewed the fixes again. The parent inspected their changes and ran integration validation. Findings included unsafe registry copying and borrowed strings, unchecked queue capacity, dead building-search code, duplicate visual profiles/JSON allocation, picker behavior, UI scaling/translations and missing regression coverage. The [prior review evidence](https://github.com/Globulation2/glob2/blob/910ac69c6af6ce1afa8b804860ffcb02423f1421/artifacts/runtime-terrain-review-evidence/README.md) retains the original findings and measurements at `7afc0e1c2`.

The final PR check caught newly merged master #795, which replaced detailed terrain layers with a material compositor. Both reviewers returned for that actual conflict resolution. The integrated version caches shipped appearance IDs in scenes, keeps canonical IDs and immutable registry ownership, and lets the compositor share equal materials. Legacy corner adapters apply only to built-in canonical IDs. Saved colors remain authoritative for custom thumbnails/minimaps; built-ins use catalog palettes. Obsolete registry visual-profile caches were removed.

Frozen format-136 numeric presentation fields are retained privately for byte-identical serialization and digests; the catalog controls detailed drawing. Map editing/loading uses registry-scoped saved-frame contracts, fixing a silent automatic-merge error that would index a built-in table with a custom ID. Development, material-authoring and map-authoring documentation explain the integrated design.

## Final validation

Environment: Linux x86-64, Threadripper 2950X, GCC 15.2, release `-O3`, repository-pinned SDL dependencies rebuilt in `build/runtime-terrain-sdl/prefix`. The included environment, dependency manifest, build log and command arrays specify exact inputs.

- **87 affected engine cases passed**, including terrain, scene extraction, pathfinding, turn-engine checks, custom LAN transfer and untrusted files.
- **818 headless unit cases passed**; 18 display cases were skipped in that run and then **all 18 display unit cases passed separately**.
- **56 UI/rendering cases passed**, including editor desktop/phone import, retry, scrolling, painting, cancellation and save/load; software/OpenGL/HD cache behavior, fallback rendering, previews and script editor.
- **Old format-135 replay and custom verifier replay passed** their stored per-tick checksum checks (3,000 and 702 checked ticks respectively).
- **Scalar and QEMU ARM64/NEON tests each passed 251 assertions in eight cases**. This covers registry/kernels, not a full ARM64 engine match.
- Scene-boundary contracts: 2 passed; translations: 5 passed plus strict catalog audit; pinned asset packaging: 28 passed; browser packaging: 18 passed; CI policy: 20 passed; web assets: 18 passed/1 skipped; terrain asset compiler: 6 passed.
- Cheap hosted checks passed; expensive native/platform jobs were skipped. The attached hosted snapshot does not establish full platform qualification.

New material integration tests compare recipes across all 16 wrapped neighbor masks using built-in/custom aliases, ensure custom sand remains a full tile, preserve embedded thumbnail colors, and verify scene snapshot/cache behavior when reimport changes ice to trail. Registry tests preserve distinct saved timing/color bytes and digests after the renderer separation.

An initial rendering test fixture used the wrong `Game::edit` type; the committed fix uses detached pre-match authoring maps. Packaging tests must run with the repository's pinned encoder Python: invoking them with the system Python reroutes work to a subprocess, which defeats in-process mocks. The pinned invocation passed all 28. Final attached logs are the successful runs.

The previous broader run (671 engine cases) is retained at its actual earlier revision in the prior evidence; it is not represented as a final-head full-engine run. Final coverage was selected around the affected loading, scene, material, UI and dependency boundaries, with all units and old/custom replays repeated.

## Renderer count scaling

One warm-up round, ten randomized measured rounds (seed 2047), CPU29, X11 software renderer, 1280×800, zoom 1, 20 warm-up/120 measured frames, presentation disabled. Every case uses the same final binary and the equivalent saved fixtures from the [original implementation evidence](https://github.com/Globulation2/glob2/blob/96a3f949bf4f4aeca3ea521d9b940a71d5fa999a/artifacts/runtime-terrain-evidence/README.md). CPU/frame, frame-wall logs, total process wall time and peak RSS are recorded separately. Total elapsed wall includes loading. The 7-definition map retains legacy corner interpretation; comparisons among 259/1,024/16,384 custom definitions isolate count scaling with shared appearance semantics more closely.

| Definitions | Median CPU/frame | Median process elapsed | Median peak RSS |
| --- | ---: | ---: | ---: |
| 7 | 3.991 ms | 2.026 s | 241.5 MiB |
| 259 | 4.006 ms | 1.982 s | 242.1 MiB |
| 1,024 | 3.988 ms | 2.017 s | 244.9 MiB |
| 16,384 | 4.064 ms | 3.734 s | 329.8 MiB |

Host load was about 25–37 on this 32-logical-CPU host. The mean paired 16,384-vs-259 CPU delta was +7.39%, with a bootstrap 95% interval of -7.93% to +20.05%; the 16,384-vs-7 interval was -11.52% to +20.89%. **These noisy measurements are inconclusive.** They do not pass or establish a repeatable regression against the required performance gates. Peak RSS includes parsing/loading high-water allocations.

The previously reported 24% frame-CPU reduction and 59% headless-memory reduction compared the old renderer at `7afc0e1c2` with `ad8406ed6`. They remain historical results in the linked evidence and must not be attributed to this final material-renderer revision. Final full-match before/after performance was not rerun after integration.

## Reproduce and remaining coverage

`payload/integrated-commands.json` records native build, affected engine, all headless units, UI and replay invocations. `python-commands.json` includes the separate display-unit run and Python contracts. `standalone-commands.json` records scalar and cross-compiled/QEMU commands. `render-command.json`, `render-bench.py`, individual invocation records and `analyze-render.py` reproduce the renderer experiment. The internal SHA-256 manifest covers every payload file. Earlier fixture files are available in the linked implementation/review evidence; no local authoring JSON is required to load them.

Full Windows, native ARM64 and browser/WASM engine per-tick checksum comparisons, additional representative full matches, quiet-machine timing and GPU performance remain outstanding. Desktop/phone screenshots are layout tests on Linux, not physical phone testing. The PR remains draft until the original qualification requirements are met.
