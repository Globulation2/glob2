# Final adaptive-default validation

Adaptive prestige is enabled by default at the user’s explicit direction. New strategies use `prestige.enabled=true` and adaptive `prestige.max_pipelines=4`; normal prestige victory remains required. Timed-only games remain inactive. Saved canonical settings and older missing-cap migration retain their established values.

Final SIM118 / save193 / network75 source is frozen in [final-source.json](final-source.json). Master was fetched at8d5293c436c1a65e8ad47459e738152644fcf137; its unrelated localization change used117, so this feature reserves118 without merging unrelated changes.

- 508 native cases pass, zero failures/errors/skips, with the new default active.
- 76 Python strategy/configuration checks pass, including default enabled/cap4 and explicit opt-out.
- Two additional online-version checks pass;310 Markdown documents have zero errors.
- Golden match regenerated and verified by the optimized engine, with no rejected orders. Current protocol75 verifies; an otherwise equivalent valid-CRC protocol74 record is explicitly unverifiable. Replay version boundaries are covered in the native suite.
- Current full-game Prepare save resumes identically to uninterrupted execution. All128 per-tick world, replay and snapshot checksum records match between development and optimized builds and against the frozen SIM117 segment with the same serialized strategies.
- Real191 migration and current/legacy continuation, investment/cancellation state, unsafe starts, acknowledgement, stale identities, custom catalog limits and bounded pipeline requests are covered by native cases.

The288-game comparative assessment remains frozen atSIM117. [Source comparison](final-source-diff.json) proves that the final differences are the requested base default, matching documentation/default tests, and version/golden identity; the prestige algorithm is unchanged. All study arm parameters were explicit, so the later default flip does not alter the recorded comparisons. Independent analytical recommendations are retained as evidence; the user’s instruction controls the final activation decision.

Only Linux x86_64 GCC13.3 was available. Other supported-platform execution checks and manual play were unavailable. No runtime dependency was added.

[Full checks and hashes](final-verification.json) · [Assessment](assessment-summary.md)
