Independent read-only PR1161/source readiness review: feature983b482e52c5db80c36024aa5b2624f579ccac07 against origin/master21f432e0c0d10751e7189af37b0acfe2c19eeff6. No edits/builds/therig use by reviewer.

Not ready to merge.

P1: FileFormatVersions.h281 uses format159 for cached hiring. Master already uses159 for turret targeting and is format161. Game_io.cpp313 and building loads consequently expect hiring bytes in master saves that never wrote them. Integrate a fresh format gate while preserving master's fields and durable compatibility floor; verify existing master saves load with empty hiring state.

P1: Feature SimRevision.h19 is55, master58; no regenerated FourSquares1.g2mr appears in the feature diff. Actual merge conflicts span about20 files, including GameAI owner boundaries, WorldCapture, executor lanes, Unit state, Game_io and replay gates. Resolve real integration conflicts, allocate a fresh simulation revision, regenerate golden and validate integrated revision. Existing cohort evidence applies to983, not an untested merge result.

P2: HiringPipeline.cpp564–565 reserves all27 possible job slots for every vacant building, regardless of supported jobs. The96MiB match reservation limit throws during admission. Staffing20 reserves26048bytes/building; published+pending bounds saturate around1932vacant buildings across the match, far below1024/team. Tighten reservation to actual supported jobs and exercise a real large saturated world; existing saturationtest only injects a full bound.

Architecture otherwise coherent: workers borrow shared immutable world snapshots, owner route binding does not initialize coldfields, generation leases/locks protect shared routes, live admission remains authoritative, fixed publication and save continuation are explicit. No committed development artifacts or surviving old population-wide hiring consumer found.

Rewrite obsolete material-only isolated-prototype description around production all-job hiring. Preserve historical evidence in comments. Say no detected significant primary differences with practical noninferiority inconclusive, not proof of zeroimpact. Fresh platform evidence Linuxx86-64 only; reviewer performed no new integration/platform verification.
