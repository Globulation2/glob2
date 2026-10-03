# Gameplay recording PR 622 evidence

Source commit: 66fb28ba7. Local platform: macOS arm64, pinned SDL3 dependencies, FFmpeg 7.1.

The archive contains software/OpenGL and two-peer threaded LAN videos, chapter manifests, chronological event journals, initial states, replay checksum comparisons, frame timing CSVs, decoded sample images, test/build logs, CLI checks, and the independent review with resolved findings. Private profiles and certificates are excluded.

Validation: 446 unit cases; six recording cases with real encoding; three integration cases each under software and OpenGL; both LAN peers with identical checksum sidecars; eight Python manifest/decoded-media tests; four related engine cases; strict translation validation. WebM results extraction was probed as VP9/Opus. Frame timing is a small 60-tick 800x600 fixture, not a long-match benchmark. Windows/Linux runtime and physical audio-device latency remain unverified locally.

SHA256: 0d3f48f8c7d3b0aac091dfaf76a2323681d7f862506028b69306e41fcf257783

## Integration refresh before merge

Source 5c61e1d93 incorporates master f597f2235 and labels turn-protocol games as multiplayer. The additional archive includes clean-source release build output, all 485 unit cases passing with real FFmpeg enabled, three recording integration cases, both LAN peers with matching checksum sidecars, and all five non-benchmark TurnEngineHarness cases passing. Earlier OpenGL evidence remains scoped to 66fb28ba7. Hosted CI remains pending at evidence publication.

Additional archive SHA256: d6cb17a2332e78779349efccd75ad0c1dcc97242d32f68459a2f53b848dc850f
