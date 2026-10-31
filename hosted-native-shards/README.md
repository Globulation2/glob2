# Hosted native shard failure classification

These hosted failures are baseline fixture/expectation drift associated with upstream [terrain mainlining, PR #1308](https://github.com/Globulation2/glob2/commit/cbc20d2aabc364f7727439659001d29ffc741c25), not changes introduced by recording PR #1313. No source edits were made during classification.

PR head `9b2162f3bf346cd7f115ab25ba3c1d09d0ad51c7`; compared baseline `f3a60235b0afa8bf5df1634b65fb1ee8d83d0ab2`. Hosted binaries report merge revision `2eac63297eaf63579559a9e6e03fb7949a92f9bf`. Its tree `dddc6052403f959c9342dd34cacf8f8e74951ba8` exactly matches the PR head tree, and all relevant test/serialization/fixture source blobs are unchanged from the baseline (table below).

## Observed failures

| Hosted job | Result | Failure |
| --- | --- | --- |
| [GCC11 shard 1](https://github.com/Globulation2/glob2/actions/runs/41244011872/job/124676613616) | 475 passed, 3 failed | IntrusivePopulationSave synthetic version164 load; RuntimeResources frozen checksum trace; TeamStatsSave stale network gate literal |
| [GCC13 shard 3](https://github.com/Globulation2/glob2/actions/runs/41244011872/job/124677136993) | 477 passed, 1 failed | BuildingStock synthetic version159 load |
| [GCC11 shard 3](https://github.com/Globulation2/glob2/actions/runs/41244011872/job/124676613618) | 477 passed, 1 failed | Same BuildingStock synthetic version159 load |
| [GCC11 shard 4](https://github.com/Globulation2/glob2/actions/runs/41244011872/job/124676613604) | 474 passed, 4 failed; unit report: 1258 passed | BuildingStock synthetic inside-service version159; IntrusivePopulationSave synthetic version166; DelayedGradientPublication synthetic header174; stale JavaScript gate literals |

- **TeamStatsSave:** `measurementReplayBoundaries()` still requires `NET_PROTOCOL_VERSION == 72`. Upstream #1308 raised it to 73. The observed diagnostic is `integrated simulation uses current replay and network gates`.
- **RuntimeResources:** all 150 actual trace rows have exactly the same seed, tick and RNG digest as the committed golden. Every checksum differs by exactly XOR `0x00800000`. The trace records the full `Game::checkSum()`, which includes MapHeader's current format version. Upstream changed that version from 186 to 187; this single header bit rotates to the observed checksum bit for this one-team/one-player fixture. Both actual and expected compact traces are retained here. This is format-header-sensitive golden drift, not evidence of recording-induced simulation divergence.
- **IntrusivePopulationSave / BuildingStock:** these cases synthesize old files by passing current `game.save()` output through `LegacyPopulationOutput` and patching the version to 164/159. The current format187 MapHeader writer omits the retired terrain-requirements list, whereas historical format134–186 loading still consumes that field. The synthetic adapter was not updated to restore it. The two failures therefore occur while loading synthetic historical-layout fixtures, with unchanged PR/base serialization/test code. These failures alone do not establish a failure to load genuine historical saves; those should remain part of a focused upstream repair audit.

### Additional GCC11 shard 4 details

- BuildingStock's inside-service case uses the same synthetic historical writer as the entering-service case and fails loading version159 at line226.
- IntrusivePopulationSave's pending-jobs case emits current output through LegacyPopulationOutput with the version relabeled166 and fails loading that synthetic fixture at line203.
- DelayedGradientPublication's `private174` test writes a current MapHeader through a writer that only changes `versionMinor` to174. It does not restore the retired count/list field; the historical parser consequently reaches EOF reading `count`. The source is unchanged from baseline.
- JavaScriptCompatibility repeats the previously classified 186/187 and 72/73 literal mismatch at lines139,140,142.

None of these failing cases records video. The selected shard4 unit report has 1258 passing cases (including 24 recording cases) and no unit failures; exact retained case metadata is in classification.json. The new source blob hashes are listed below.

## Retained evidence

Each job directory contains only its original small engine JUnit report (and the unit report when available), failing-test log excerpts and artifact metadata/URL. GCC11 also contains the actual and expected 150-row resource traces. `classification.json` retains exact failure messages and matching blob hashes. HTTP byte ranges fetched approximately 818 KiB total from about 704 MB across four artifact archives; large screenshots, saves and unrelated reports were not downloaded or packaged.

| Relevant source | Identical PR/base blob |
| --- | --- |
| `src/team/IntrusivePopulationSaveTest.cpp` | `49638fa7d8b3fba11e292a0372b68cfc7f1ce996` |
| `src/building/BuildingStockTest.cpp` | `e7343ac21d5f9aa9c0ff436ac050785ea05d921c` |
| `src/team/stats/TeamStatsSaveHarness.cpp` | `72ce163e15b061cf3110a5056bf1660a5b6474a7` |
| `src/map/TerrainResourcesHarness.cpp` | `2a3145e6d969acf5e516395e8748749539b36732` |
| `test/support/LegacyPopulationOutput.h` | `e997be62338a1112e618a648c5193602c66d9258` |
| `test/fixtures/resources/seeded-compositions-units.trace` | `858f0572c35856b24b886d37f9ae47ad59d59a62` |
| `src/app/Version.h` | `d1d608c3c93ccba9265d61bd53dae68c8489f747` |
| `src/app/FileFormatVersions.h` | `87eb5036531b4dcea94fd38e6674183a966a1d83` |
| `src/map/io/MapHeader.cpp` | `125bb0a45c6eb3e67508899abc0b9416bf8e9465` |
| `src/game/Game_io.cpp` | `c080da6b46ab50b9fbb283043bf614dba512fe39` |
| `test/support/EngineFixtures.cpp` | `6d0dc35a72ee80f475db10f0fe46d01ea82ba7e7` |
| `src/map/gradient/DelayedGradientPublicationHarness.cpp` | `69d297e16815d75ad4abc7905cb8750a00edb7ff` |
| `src/scripting/javascript/ScriptCompatibilityTest.cpp` | `0380c5e70fd9bf09c494544fd7bf68f07ca63336` |
