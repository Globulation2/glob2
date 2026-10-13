# Authoritative entity compaction: local evidence

Tested source: `3122cbe34433a0211faa34ec00bdeef9a09edca3`; base: `277510840054b1416f2defd7d86fe1431dd0ddc0`.
The candidate was tested before committing; its validated binary diff is byte-identical
to the committed change (see the manifest's SHA-256).

- [Download validation bundle](entity-state/evidence.tar.gz) (4.5 MB).
- [Field-by-field decisions, layouts, results and limitations](entity-state/decision-inventory.md).
- [Entity paired measurements](entity-state/entity-comparison.json).
- [AI match paired measurements](entity-state/ai-comparison.json).
- [One/four worker replay/save/checksum comparison](entity-state/worker-comparison.json).
- [File hash manifest](entity-state/evidence-manifest.json).

249 focused release cases and 15 editor/rendering cases passed. Ten matching release
renderer PNGs are included. Ten rotated entity pairs retain all five representative
baseline/candidate canonical saves and traces, together with aggregate results from
all pairs. The bundle also contains replay bytes, checksum sidecars and final saves
for representative one-worker baseline/candidate runs; the comparison report checks
all four baseline/candidate one/four-worker runs.

Runtime conclusions remain limited: short-workload timing intervals are broad and
the AI setup/save CPU gates are inconclusive. Material promotion, independent
unit-only/building-only runtime comparisons, Callgrind, whole-frame measurements,
and non-Linux execution remain outstanding. This is draft qualification evidence,
not maintainer acceptance or a full hosted verification result.

Generated files and working notes are isolated on this evidence branch; they are
not part of the implementation PR's Git tree.
