# Demand-driven building initialization restored

Tested commit d1c0d4d50109b6f7d30f843368d30406f515b84f, base 486a2230a4ddb257ba9dee34d3aace05f2260d6f. Creation/construction hooks from 69e6c86014c50053d36e840a8cfe5290226b7163 removed; primary-hire prewarming retained. Legacy queued proactive work continues when loading older saves.

macOS 26.6.2 arm64, Apple M3, Apple clang 21.0.0. Existing managed dependency prefix build-dd8d40b0b2e044b88374d32e and Homebrew dependencies; full compiler commands in build.log.gz. Development build (`dev_fast=1`, `-O0 -g1`) for correctness only. Built working-tree source exactly matches tested commit. Build command in metadata.json, test commands in commands.sh.

197 distinct cases passed: 138 focused engine, 41 replay/protocol unit, 18 turn-engine. Golden trace regenerated and verified again in the turn suite. SIM_REVISION 72, replay floor 173, save floor unchanged at 58. Simulation revision check passed after fetching master. JUnit and tests.log attached.

Coverage: lazy creation/upgrade/cancellation, primary-only preparation, assigned-unit Pending preserving position/assignment/RNG, binary/text save continuation, older-format loading, fixed deadlines, native participant determinism, replay acceptance and turn reloads.

The archive includes actual format-172 queued and pending binary Game saves from previous qualification. Appended only the standard 36-byte client footer (four ff bytes plus 32 zero bytes) so the CLI can read the Game-only fixtures. Four `game run --load-game ... --ticks 16 --compute-threads 1|4 --output-dir ...` runs passed, with exact commands and outputs archived. Queued initial checksum 3717896587, pending initial 1134481355; all final checksums 3419033173. These compare endpoints, not per-tick platform traces. Initial probes against Game-only files failed at the absent GUI footer; the CLI also cannot read direct text fixture files. Native binary/text continuation tests passed.

No full-suite green claim, new performance claim, cross-platform per-tick checksum comparison, rendering/manual play, or release packaging verification. PR remains draft. Focused coverage targets changed activation/scheduling and compatibility risks. The archive contains generated saves, replay/match evidence, checksums and per-case provenance; disposable profiles excluded.
