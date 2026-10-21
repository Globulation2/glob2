# Map generator verification

Evidence for code commit `8ee1242f4507fb40d2b23efc49d8a3f727ce4961` and PR [#1246](https://github.com/Globulation2/glob2/pull/1246).

This branch contains review evidence only; none of these archives are shipped with the game.

The archives contain request matrices, result tables, previews, generated maps, saved games,
build and contract logs, per-map validation manifests, and paired AI economy records.
Game log files ending in `.economy.log` are labelled subsets retaining GLOB2_MEASURE,
GLOB2_AI_FINAL and GLOB2_RUN records; the manifest records each original log's SHA-256.
Raw high-volume AI sampling lines and executable binaries are retained locally and omitted.

Native environment: Linux x86_64, GCC 15.2, development `-O0 -g1`; SDL3 from the existing
configured prefix. This is generation/survival/access evidence, not human play, competitive
balance, cross-platform checksum equivalence, or release-performance certification.

Historical validation manifests record the actual intermediate source/binary hashes. Later
changes to unrelated generator code or documentation do not retroactively change those results.
The combined regression log and final commit source manifest will supplement this evidence.

## Downloads

- [braided-delta-resource-redesign.tar.gz](braided-delta-resource-redesign.tar.gz) (16.5 MiB)
- [comb-natural-shores.tar.gz](comb-natural-shores.tar.gz) (13.5 MiB)
- [coral-expansion-nodes.tar.gz](coral-expansion-nodes.tar.gz) (8.0 MiB)
- [glacis-waterfront-crops.tar.gz](glacis-waterfront-crops.tar.gz) (17.2 MiB)
- [honeycomb-open-cells.tar.gz](honeycomb-open-cells.tar.gz) (12.2 MiB)
- [irregular-clearing-starts.tar.gz](irregular-clearing-starts.tar.gz) (17.5 MiB)
- [lava-shield-no-vents.tar.gz](lava-shield-no-vents.tar.gz) (0.1 MiB)
- [plantations-island-workers.tar.gz](plantations-island-workers.tar.gz) (19.4 MiB)
- [portage-lakes-open-fields.tar.gz](portage-lakes-open-fields.tar.gz) (12.9 MiB)
- [shattered-coast-expansion.tar.gz](shattered-coast-expansion.tar.gz) (12.2 MiB)
