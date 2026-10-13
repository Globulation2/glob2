# Current entity-state field decision inventory

Performance baseline: former origin/master b4c463a439953059c9ac058902cff8a652e87f02.
Selected shipping layout: compact-original, with unit compaction and audited narrow
building state/loads, retaining baseline Building.h live binding order. Selection
uses the completed240 whole-match runs; rendering/further comparisons are skipped
at the user's direction. Performance base b4 differs from final integrated5750ba7296
including master35b84d4ff; no final-head performance rerun is claimed.
Observed catalog maxima choose workloads, not storage bounds. This table records
why each value is directly narrowable, remains wide or needs more evidence.
Current release source/binary manifests and measured layouts are indexed in
artifacts/state-performance/layout-screen/reviewer-layout-report.md. Earlier
277510-based measurements are preserved as superseded evidence, not current claims.

| Field | Decision | Initialization / runtime / boundary evidence |
|---|---|---|
| Unit medical/activity/displacement/movement | Direct, Uint8 backing | init/actions/medical transitions use named constants; load readState validates raw Uint32 maxima, displacement parity and movement gap 10 remain rejected; enum arithmetic promotes to int; no exposed wide references |
| Unit action | Direct, Uint8 selector | init and UnitAction select global IDs 0..16; loader retains NB_ABILITY validation; global Abilities remains unchanged; performance indexes and checksums promote numeric values |
| Unit direction | Direct, Uint8 | UnitGeometry tables and masked rotations produce 0..8; initialization sentinel 8; loader signed value validated before cast; no output pointer to direction |
| Unit dx/dy | Deferred, Sint32 | init/movement produce -1..1 and loader validates; map searches and exit APIs take int pointers, including failure output writes; left unchanged rather than introduce adapters without separate failed-output measurements |
| Unit level[17] / constructionLevel | Direct, Uint8 | init/catalog levels 0..3, training validated targets, conversion/tiling copy levels; historical worker migration uses max of two validated values; load validates signed raw values; editor wide pointer binding replaced by explicit byte binding; mixed std::max/clamp calls cast to int |
| Unit learning flags | Mask | init from race, refresh from catalog, load nonzero booleans; runtime setters preserve adjacent bits; historical worker migration and observations use bool accessor; save/checksum retain ability iteration order; initialization clears unused bits |
| Unit service/wide cargo/medical recheck/valid target flags | Remain bool | Existing masks not required for measured unit reduction; optional packing deferred until additional savings/access costs demonstrated |
| Unit destination/carried material | Direct, Sint8 | -1 sentinel; destination ends at FEED=18; cargo material namespace MaterialCount; selection/delivery/conversion/catalog policies preserve namespace; loader validates signed temporaries before cast; shifts/checksums retain int promotion |
| Unit animation clocks | Direct, Uint8 | init/load zero; UnitStats sets 20, UnitMedical sets 8; tick decrements only above zero; renderer arithmetic promotes to int; intentionally absent from save and legacy checksum |
| Unit terrain-health remainder | Direct, Sint16 | init zero; accepted loader range -255..255; exposure rate accumulated in int before /256 and %256; preserves signed debt and HP clamp/evaluation order |
| Unit isDead | Remain Sint32 | Arbitrary signed values accepted and participate in checksum; bool conversion would normalize values |
| Unit speeds/delta/performance/experience/HP/hunger/timers/RNG/generations | Remain wide | Historical and current continuation extremes; no change in limits or movement policy |
| Building lifecycle/result | Direct, Uint8 backing | init/construction transitions named constants; loader validates Uint32 maxima before cast; save/checksum numeric meanings unchanged |
| Building maxUnitInside | Direct, Uint16 | init/update from validated catalog, loadCrossRef accepts 0..65535; rejects signed negative/full-width exceptions before cast; no Sint32 reference exposure; arithmetic promotes to int |
| Building minLevelToFlag / minWorkerLevelToFlag | Direct, Uint8 backing | init zero, validated orders/scripts, editor max 3; signed loader 0..3 checks before cast; tiling copies values |
| Building priority | Remain Sint32 | Despite ordinary orders -1/0/1, loader accepts unbounded Sint32 and oldPriority preserves it; no new rejection rule introduced |
| Building unitStayRange | Deferred, Sint32 | Loaded signed interpretation accepts 0..32767; editor also exposes wide pointer/max binding; potential two-byte reduction requires separate API migration, not needed to establish unit savings |
| Building identifiers | Deferred, Sint32 | Variant IDs, family IDs, origin IDs and funding IDs have distinct namespaces; origin -1 sentinel and configurable catalogs; no content maximum assumed as bound |
| Building staffing | Remain Sint32 | Most staffing loaders unrestricted signed values; retained continuity fields remain authoritative |
| Building ratio/origin ratios | Deferred, Sint32 | Save range 0..32767 but editor binds raw array pointers and production APIs expose Sint32&; narrowing requires additional setters and consumer migration; builtin origin array outside state counted separately |
| Building percentUsed | Remain Sint32 pending transient proof | Loader range 0..32767 alone is not sufficient: swarmStep increments committed selections before allDone/reset; saved jobs and changed preferences require a transient-bound audit |
| Building unitsFailingRequirements | Remain Uint32 | Loader full Uint32 values, increment-only workload evidence cannot prove a lossless bound |
| Building localMaterials/wishedMaterials | Deferred pending exceptional-path/API work | Fifteen slots remain Sint32; materials alias selects team/private; loadCrossRef restores cached demand. Current master rejects negative loaded private stock (plan's older historical-negative premise does not apply). Full-width positive stock and signed demand still require a lossless exceptional representation before narrowing. No common-path content limit imposed. |
| Building exchange masks | Remain Uint32 | Preserve accepted high bits |
| Building clearing/lock/live-only flags | Deferred | No mask proxies/bitfields; additional layout and access benchmarks needed before retaining optional packing |
| Building HP/production timers/ammunition/RNG/generations | Remain wide | No independent bound proof; computation unchanged |


Measured unit saving is96bytes per authoritative state, snapshot view and live
unit on this x86_64 toolchain (360→264,384→288,440→344). Baseline/units-only
buildings remain352/408/1960; selected compact-original is336/392/1944. The
unselected padding control restores352/408/1960. Padding control is experimental evidence,
not a proposed long-term design. See the reviewer layout report for all offsets.

Stock/demand remain all15 full-width slots; no promotion block/sidecar or new
material bound exists. Team inventory remains wide. Preserve stream widths and
field/checksum order, accepted values, enum gaps and sentinels. Current save158,
minimum58 stay unchanged; performance-base revision53 and final revision54 belong
to master behavior changes. This representation
work does not justify a new simulation version or dropping old save behavior.

Four current-base test failures reproduce without compaction; final candidate
coverage must distinguish them from introduced failures. Completed whole-match estimates range−3.63%…+0.73%, with confidence limits
permitting roughly+2.35%…+4.90% slowdown in some rows; no universal no-regression
claim is justified. Final-head functional/replay validation and evidence publication
are recorded separately. Rendering and further comparisons are waived; independent
confirmation and platform execution remain unavailable. A microbenchmark slowdown alone does not reject a candidate;
user acceptance concerns total CPU and data size, with uncertainty reported honestly.

Selected inline saving:96×unit count+16×building count for authoritative copy
bytes and populated record/view payload. Live object sizeof reductions match those
values; allocator/capacity/lease effects must use the measured retained-memory rows.
No building padding control or pointer/reservation reorder ships.
