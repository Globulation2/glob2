# Renderer refinement verification — PR #1151

Feature: https://github.com/Globulation2/glob2/pull/1151

Final head: `d2b4e5736`; base: `f9eae2549f608f804c240709df819bfe5f09f3fb`.
Final runtime builds/tests use `94d388ed8`; the final follow-up only clarifies documentation. Full revisions, source/binary hashes, flags and commands are in `render-refinement/final-metadata.json` inside the archive.

Download `verification.tar.gz`, verify `SHA256SUMS`, and extract it. The internal `files.json` hashes every included input/result. The feature PR contains production code, tests and existing architecture documentation only. Executables, object files, profiles, rejected performance experiments and development notes are excluded from this evidence bundle.

## Review and resulting implementation

Three agents independently reviewed and refined scheduler, terrain and raster code, then cross-reviewed revisions. Final public-view and integration feedback produced two additional fixes. `review-summary.md` records the feedback and resolutions.

- One graphics admission/completion protocol for native and browser executor dispatch; shared scene cap for owner execution; cancellation completion wakes a slot-blocked owner. Captures retire outside both locks before completion publication.
- Changed-cell-first material planning, one owner-held result slab, canonical discovery-mask retirement and one shared alpha classifier. A regressing scalar classifier and duplicate scheduler shortcut were removed before final validation.
- Bounded retained tile/reference workspaces, centralized segment cleanup, clipped/transparent no-op elimination, visible fallback strips, fully populated uninitialized source arrays and opaque row copies only for provably disjoint positive-stride backing ranges.
- Positive, negative-source and negative-target alias traversal, padded pitches, clipping, cancellation, admission, ownership and source mutation/destruction regressions. A preexisting borrowed-target test now fences the backend before reading and explicitly covers all execution modes.

## Native validation

Linux x86_64, AMD Ryzen7 6800H, GCC13.3, SDL3.4.16 and the existing pinned dependency prefix. Fast debug `-O0 -g1` for contracts/integration; ordinary release `-O3` for performance. Both native build targets succeeded. Final small source changes were rebuilt with focused affected validation; no dependency or simulation code changed.

- Initial full unit run: **1,108 cases**; two failures. Both reproduce on unchanged baseline binaries. The SurfaceCoverage failure observed deferred pixels without flushing and is corrected; final **48 focused unit cases pass**, including both new negative-stride cases. The unchanged fast-debug farming100ms CPU assertion still fails (candidate119/132ms, baseline127ms under load). All original display unit cases passed.
- Parallel engine integration: **230 cases;229 passed**. The unchanged version152 stock replay fixture is rejected by master's minimum replay version154. Independent baseline rerun reproduces it.
- Serial renderer/terrain/resize/capture coverage: **89 cases,89 passed**.
- Final stride-guard refresh: **22 renderer cases and4 native capture cases pass**, including OpenGL and SDL GPU cancellation/orientation. The final guard changes only nonpositive strides; prior SDL/captured positive-stride integration paths remain unchanged.
- Save/load, immutable scene publication, required-worker lifecycle, native backend switches, multiplayer golden verification and hostile-order fuzz pass. Retained saves, replay/checksum traces, match records, screenshots and videos accompany the logs. No unresolved failure introduced by this PR remains.

The exact commands are in metadata, `engine-command.json`, retained build/test logs and reproduction scripts. Both runtime and direct-device shader environment variables were supplied. Shader sources did not change; shader generation/data provenance is available in the original PR1148 verification bundle.

## Sanitizer and portability contracts

- Native executor ASan/UBSan: **6,860 checks pass**.
- Emscripten4.0.15 through Node24: serial **6,860**, pthread **6,902 checks pass**, including controlled cancellation behind a held scene slot. These are executor contracts, not browser gameplay.
- Raster ASan/UBSan oracle: **3,600 exact original-owner comparisons pass**. Capture probe verifies1,030 source mutation/destruction cases per mode, actual tiled dispatch, no-op counters, both negative-stride alias directions and reservation release.
- Terrain cache O3 comparison: **40 exact outputs** over ten alternating rounds. Both versions compile with identical flags and unchanged compositor/dependency objects (hashes retained). The probe runs synchronous owner callbacks and excludes production scheduling.

## Performance scope and raw evidence

Baseline release executable was built at `a7515f44a`; its executable inputs are identical to base `f9eae2549` (only documentation differs). Asset packages match across all **7,470 files**. Source/binary/fixture hashes are retained.

182 release processes cover seven alternating pairs for each of13 scenario/mode combinations, at1280x800,4 compute threads, render cap2,120 measured frames and20 warmup frames. Every fixed-state process preserves simulation checksum **2231453127**. Raw distributions, commands, environment and compiler counts are retained. Background compilation resumed during later scenarios; those rows are observations under recorded host activity, not isolated FPS or threshold claims.

| Workload | Baseline CPU ms/frame | Candidate | Interpretation |
| --- | ---: | ---: | --- |
| Static warm map, auto |0.5401|0.5455|Parity;5.4µs difference. Zero rebuilds/jobs in warm static controls.|
| Animated warm map, auto |2.6471|2.2629|14.5% lower CPU in this compiler-free fixed-fixture comparison.|
| Animated warm map, serial |2.5669|2.3589|8.1% lower CPU, same fixture.|
| Half-scale/pan GUI, serial |5.2703|4.8290|8.4% lower CPU in this fixture.|
| Streamed map, parallel |31.9402|31.1488|Recorded contended observation;2.5% lower median CPU.|

Isolated raster probes: narrow fills on a300,000×1 target219.404→0.906ms (avoided strip traversal);12,000 hot128×128 opaque copies167.367→15.362ms. These are specialized kernel totals, not frame-rate gains. Isolated terrain sparse edits6.92/6.98→4.88/4.93µs (~30%); forced all-cell recomposition with prepared/reused materials remains at parity. Neither probe measures cold startup.

Original baseline/candidate captures match for12 of13 scenario/modes. The remaining difference is exactly72 pixels in the live tick-rate readout (`-` versus`0.0`, bounds620..634,3..9), caused by its one-second measurement window. Every other pixel matches. Original captures, crops and diff are retained. A600-frame fixed-input GUI capture refresh stabilizes the readout; all **12 full baseline/candidate/auto/serial/parallel captures match exactly** for pan and fractional-offset cases.

18 interactive10-second runs (three alternating pairs per mode) exercise actual simulation/scene/render contention. Per-run distributions are retained rather than pooling percentiles. Median run p99 draw/present auto7.324→8.026ms, serial9.194→9.243ms, parallel11.201→9.421ms; scene-age results also vary. Background activity is uncontrolled. These mixed samples establish neither smoother gameplay nor portable activation thresholds. Interactive mode uses isolated default cloud settings (its branch does not honor PROFILE_CLOUDS).

## Limits and author acceptance

Automatic execution remains immediate and serial; experimental worker groups/recording remain opt-in. No new runtime dependency, simulation rule, save format or sim revision is introduced.

Actual game integration was tested on Linux only. Windows, macOS, mobile, actual browser gameplay/checksum comparisons and real HD asset-pack playtesting were unavailable. Synthetic HD cases and Node executor contracts do not establish those platforms. TSAN could not initialize on this host in prior qualification (`unexpected memory mapping`); ASan/UBSan and stress do not prove race freedom. Automated interactive/capture checks do not replace maintainer playtesting. Cheap hosted checks are not engine verification.

Author acceptance: the linked evidence is sufficient for the scoped refinements with these limits and the two reproduced baseline failures. Default concurrency selection remains unchanged.
