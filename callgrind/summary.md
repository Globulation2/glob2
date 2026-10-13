# One indexed turret shortlist planner: implementation and Callgrind

The production scan-based shortlist enumerator has been removed. Live and immutable read adapters share one occupied-cell traversal. It supports stock octants, custom rectangular footprints, wrapping and repeated wrapped representatives. Retained and temporary occupancy chunks use the same builder. The original game selector remains unchanged as the disabled-experiment behavior and independent head oracle. Forward geometry enumeration now exists only in the test oracle.

Retained index allocations remain bounded at 32 MiB across an observation lineage. When chunks cannot be retained, the query rebuilds only the chunks it needs and feeds their records through the same traversal. Exhaustion is tested while old immutable generations remain held.

Callgrind regions contain 2,000 planner queries each; fixture setup, capture, index maintenance, warmup, assertions and output are outside collection. Baseline is the first unified indexed implementation, not the old game selector. Both builds use native GCC 13.3 -O3 release engine-test binaries with function symbols. Tool: Valgrind/Callgrind 3.22.0.

| Fixture | Before instructions/query | After instructions/query | Reduction |
|---|---:|---:|---:|
| empty | 2,659 | 306 | 88.5% |
| combat | 9,467 | 5,645 | 40.4% |
| custom-wrap | 147,510 | 146,193 | 0.9% |
| temporary | 33,935 | 31,344 | 7.6% |

Profile-driven changes restrict queries to intersecting chunk spans, reject excluded teams/occupants before encounter generation, simplify empty-cell checks, reserve bounded ranking scratch, and compare deduplication identities as an exactly equivalent packed integer rather than a tuple. All remain inside the one planner. The first optimization round slightly regressed dense custom wrapping; the final identity-comparison change removed that regression. The intermediate dumps are retained in iteration-1/.

All 54 focused release cases pass, including original-head fidelity, test-only alternative-order geometry, 90 map/footprint/range combinations with retained/temporary storage and blockers, index-budget exhaustion, existing building combat and shared-worker lifecycle tests. Twenty paired Numbi/Castor continuations preserve complete AI-order, checksum and save byte comparisons. An earlier broad run passed 53 cases and timed out the battle matrix at 120 seconds during other host builds; the final run at -j2 with a 300-second per-case limit passed all 54.

Native measurements retain two process-order rotations, each with one warmup and ten rotated paired batches. Unchanged original-selector controls fluctuated substantially under unrelated compilation, so the runtime samples are exploratory. The instruction reductions above are not whole-engine elapsed-time or process-CPU claims. Capture/maintenance and integrated producer interference remain outside the profile.

Verified platform: Linux x86_64 native release. Other supported-platform comparisons are unavailable. The tested base remains f9eae2549f608f804c240709df819bfe5f09f3fb; current master advanced during development and refreshed integration against it has not been verified. The feature remains a shadow/research prototype; live match configuration, persistence and simulation-version/replay/network qualification remain separate unfinished milestones. Hiring and authoritative firing behavior were not changed by this refactor.

Raw profiles, annotated functions, exact source snapshots, binary hashes, logs and native timing samples are retained under before/, iteration-1/ and after/. Machine-readable figures and limits: summary.json. Final verification: ../single-index-final.xml and ../single-index-final-rerun.log. Compiler log: ../single-index-build-identity.log. Paired battle evidence: ../single-index-evidence/.
