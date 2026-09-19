# Final generator review: verified snapshot

Reviewed `../verified/DrownedForestGenerator.cpp`, compact preview `../final/retest21-100260.png`, its telemetry report, and `../verified/play-map-equivalence.json`. This is a code/visual review plus prior saved-game evidence, not an independent completion claim for the running final sweeps.

## Compact layout and repair

The compact seed100260 result retains the intended composition: irregular islands, clear sandy links, open home meadows and concentrated structural woodland around timber cuts. At zero ambient wood the concentrated shoulders are more conspicuous than on default maps, but do not read as a repeated grid. The two home locations fit the limited island space and leave separate forward sites. This tiny minimap establishes overall composition only; it does not resolve individual gathering lanes, building footprints or resource containment.

Telemetry confirms the resulting two cuts have substantial coastal savings (73→14 and78→14 steps), two roomy home candidates retain two independent exits, and both survive useful-shortcut filtering. Both home farm growth potentials are about18.44. The chosen side mask is3 after all four combinations, on landscape attempt193.

The compact side search is a substantive geometry repair: each alternative starts from the identical pre-meadow landscape, without consuming random draws, and chooses the meadow-side combination with the most viable homes. It does not weaken room, independent-exit, food, contested-meadow or actual shortcut validation. Larger maps execute only the original side choice. The saved256 seed7/19 maps are byte-identical to the tournament fixtures according to the preserved comparison artifact, making those gameplay results applicable to these two maps.

## Search budget

Raising the compact-only bound from192 to512 is justified as bounded tail coverage after the actual repair. A concrete request succeeds at193 with corrected side assignment; rejecting it exactly one proposal earlier is not evidence of an impossible map. The bound remains finite and preserves every acceptance requirement. It is nevertheless a reliability/time tradeoff, not a proof that every input succeeds. The new cap permits about2.67 times the former maximum proposal count; wall-clock impact must come from final timing data rather than this ratio, because proposals have very different costs.

Keep the final random/control sweep results and worst-case timings with the generator. Those sweeps were still running when this review was written, so this document does not pre-claim their success. Static-side search also stops once enough static homes exist; a later actual-world shortcut or contested-meadow failure still advances to another landscape rather than trying all remaining side combinations.

## Correctness reviewed

- Swarm gathering-face fallback uses currently walkable faces reachable before hypothetical construction, then runs the original full-world room helper. It fixes the worker-on-future-footprint false rejection without pretending arbitrary town pockets are valid entrances.
- Local building-room proof preserves global acceptance for tested cases. The independent800-case harness caught seven false accepts from blocked external roots; the added guard eliminates them. Exact failure strings and ordered rectangles now match across all800 cases. Evidence is in `../local-room-check/`.
- Actual unit walking masks and home distances determine shortcut benefits. Conservative eventual-growth masks are limited to permanent-route guarantees.
- Bounded opened-cut floods retain exactly the previous savings thresholds; lazy reverse-mouth floods and already-certified home skips retain existence semantics. Final GenerationService validation remains active.

## Remaining limits

The seed19 gameplay review found two major Maxima starvation episodes associated with lost or distant feeding buildings, including one on the strongest farm. Accessible wheat remained. This is not evidence that a larger pond alone would cure those losses. Initial farm growth potential varies by almost3x on that map; that remains a fairness consideration, with no calibrated minimum established by these runs.

Earlier controlled real-worker harvesting proves one timber cut opens and shortens an actual home-to-meadow route, but does not show autonomous AIs reliably recognizing its strategic value. Four-rotation late saves show expansion, persistent walking connections and no crop invasion of initially crop-free grass components. Human play remains the best check of pacing and map feel. The standard shared swimming-distance metric ignores algae blocking; the late-review analysis explicitly corrected for that when measuring swimming usefulness.

No tracked files were modified by this review.

## Dense-request tail extension (final22 addendum)

Reviewed the complete diff between `../verified/DrownedForestGenerator.cpp` and `../verified/tail-fix/DrownedForestGenerator.cpp`. The only behavior change is the attempt limit:128² remains512; exactly256² with8 teams becomes256; every other request remains64. No terrain rule, parameter interpretation, RNG call, acceptance condition or earlier-attempt path changes. Consequently any request succeeding inside the former limit retains its successful landscape and random-stream state. This is a narrow tail extension, not a geometry change to previously generated maps.

Three old-limit failures now produce valid maps:100801 at attempt70,101010 at66,and100951 at106 (their frozen JSON reports were inspected). These are direct witnesses that the difficult combinations are feasible and that64 attempts was insufficient. High sandbar connectivity and thick necks make the requirement for8 individually useful home-accessible shortcuts demanding; retaining the strict contract and paying for extra bounded search is reasonable here.

The100801 timing artifact records15.26 CPU seconds and56.66 wall seconds on the contended Mac. That is a material latency tail and should be disclosed with final profiling rather than hidden by average timings. The new bound is finite, and successful examples do not prove universal reliability; the fresh Linux matrix remains the broader validation. If future requests continue to exhaust this cap, revisit destination coverage rather than increasing it indefinitely.

Independent successful-prefix confirmation: generated dense request seed100973 with both frozen binaries, using the same output basename to avoid serialized map-name differences. Both succeed at landscape3, their full JSON reports match, and the saved maps are byte-identical (SHA256 `50e71b1301120050195c2491cc0c7d50f0da4cf010d188bdd02aba0558fe7046`). Runnable `tail-prefix-check.py` and `tail-prefix/results.json` preserve exact commands and both outputs. This empirical check supplements the complete source-diff argument; it is not an exhaustive output comparison across all seeds.

## Independent-exit flood cutoff review

Reviewed the proposed one-line replacement of the full `stepsFrom` with `floodFrom(...,60).steps` inside `independentExits`. The shared flood records vertices at distance60 by expanding distance59 and only skips expanding vertices at60. Therefore all distances0–60 are exactly preserved. Goal selection only admits distances1–60, retains the same traversal/tie order, and path reconstruction only follows decreasing distances. Its path mask, occupied dilation and next-leg available mask are identical; induction covers both legs and both route orders. No RNG or telemetry state changes. This establishes output equivalence for this specific replacement; final frozen map comparisons remain a complementary integration check, to be reported by the author.
