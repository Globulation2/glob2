# Gameplay recording PR 622 evidence

Source commit: 66fb28ba7. Local platform: macOS arm64, pinned SDL3 dependencies, FFmpeg 7.1.

The archive contains software/OpenGL and two-peer threaded LAN videos, chapter manifests, chronological event journals, initial states, replay checksum comparisons, frame timing CSVs, decoded sample images, test/build logs, CLI checks, and the independent review with resolved findings. Private profiles and certificates are excluded.

Validation: 446 unit cases; six recording cases with real encoding; three integration cases each under software and OpenGL; both LAN peers with identical checksum sidecars; eight Python manifest/decoded-media tests; four related engine cases; strict translation validation. WebM results extraction was probed as VP9/Opus. Frame timing is a small 60-tick 800x600 fixture, not a long-match benchmark. Windows/Linux runtime and physical audio-device latency remain unverified locally.

SHA256: 0d3f48f8c7d3b0aac091dfaf76a2323681d7f862506028b69306e41fcf257783

## Integration refresh before merge

Source 5c61e1d93 incorporates master f597f2235 and labels turn-protocol games as multiplayer. The additional archive includes clean-source release build output, all 485 unit cases passing with real FFmpeg enabled, three recording integration cases, both LAN peers with matching checksum sidecars, and all five non-benchmark TurnEngineHarness cases passing. Earlier OpenGL evidence remains scoped to 66fb28ba7. Hosted CI remains pending at evidence publication.

Additional archive SHA256: d6cb17a2332e78779349efccd75ad0c1dcc97242d32f68459a2f53b848dc850f

## Corrected menu capture

The original isolated menu fixture omitted Application’s FrontendTheme, producing a plain backdrop. The fixture now owns the real theme and asserts that the colony loads and advances while captured. The additional archive contains OpenGL footage and checksum comparisons with the colony visible; all three integration cases pass (69 assertions). `corrected-menu.png` is decoded directly from the new MP4. Changes are in source commit bd7446027; runtime evidence was captured immediately before committing the identical fixture.

## Final pre-merge validation

Source 5bd86c012, integrated with master eea70c0e1. Clean source builds all client and test targets. All 597 unit cases, three recording integration cases under software and OpenGL, the two-peer legacy LAN recording fixture, the menu-colony presentation/keyboard harness, and eight decoded-media Python cases pass. All 256 build-system tests pass with one optional fontTools check skipped locally. Strict translations pass. The final archive contains current footage, checksum comparisons, initial states and logs; corrected-menu.png is from the current OpenGL video. Hosted platform verification remains pending at publication.

Final archive SHA256: fb935132c44b6fd8ee608d89640d63fdbc4a5382828b36310b5593800bbb865e
