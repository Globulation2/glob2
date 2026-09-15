# Emoji balance experiments

Preserve every terrain corner of revision 3. Colonies should establish an inn and
population before contact; later compete over shoreline and crossings. No engine
or AI rule changes. Use the existing tournament runner and immutable Linux bundles.

Hypothesis A: the ten-tile crop exclusion and fertility-first seed choice delay
inn construction and food deliveries. Test closer starter crops at the same sites,
keeping the larger ambient clearing. Compare 15k population, precombat starvation,
inn completion and opening construction at fixed seeds/rotations.
Hypothesis B: terrain-only dispersion picks unequal sites. If A is insufficient,
constrain sites by a functioning near-shore town footprint and comparable resource
access, then test full games and held-out map seeds.

Tune on smiley filled grass 73001, sad filled grass 73002, sunglasses outline water
73002. Validate on other characters and fresh map/game seeds. Never pool adaptive
checks with the original 512-game survey. Report regression and unresolved bias.

## Candidate identities

- r4: move the existing 48-tile starter crops closer; six-tile inner clearance;
  retain ten-tile ambient clearing. Fertility-positive nearest placement.
- r5: r4 with wheat/wood seed separation reduced from eight to four tiles.
- r6: restore the r3 shore farms; supplement with up to 24 wheat/16 wood close by.
  Single-patch supplementation proved uneven when terrain split the eligible region.
- r7: r6 plus local site refinement, scoring nearby growing area and room. The score
  ceiling saturated on the smiley, leaving its starts unchanged.
- r8: remove the site-score ceiling; allow supplemental supplies to occupy several
  patches, preferring fertile nearby tiles. Starts may move up to ten tiles per axis.

Each immutable probe manifest uses the same generic first-experiment hypothesis
text; this index records the actual later interventions. Complete replacement C++
snapshots, bundle identities and build logs distinguish every candidate. Terrain
construction and AI/simulation code remain identical across these interventions.

The identity-r9 jobs inherit the reference map labels (including revision=r8); their actual build IDs and native result.revision identify r9. Reference labels were left immutable. Final r9 changed only non-default resource clearing; all19 standard map files matched r8 byte-for-byte.
