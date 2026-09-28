# Echo/Nicowar continuation fixture

`arena.map.gz` is a retained 128×128 two-team arena from the farming comparison
suite. Its decompressed SHA-256 is
`0f14004bc436b78c507d050f2fa13a07a604e720f49ef57fe3ea00f3b443fd7b`.
It fixes terrain and starting conditions independently of generator changes.

With game seed 6101 and Maxima/Nicowar, format 118 loses Echo's distance-field
cache on a reload at tick 4096. The first 89 subsequent orders match, but Nicowar
creates its next building at 6171 instead of 6170. The first entity difference
appears at 6171. Format 119 preserves the queued refresh work and cached fields.

Run `python3 test/check_echo_save_continuation.py PATH/TO/glob2` to compare every
team/entity record through tick 8192, with reloads at 4096 and again at 6144.
Nicowar/Nicowar checks shared-manager continuation as well. Expected traces are
produced by each tested binary's uninterrupted run, so intentional future AI
changes need not regenerate fixed output hashes. The older map remains readable.
