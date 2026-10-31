# Compiled pack: blended frames only (847166cc87 vs 071fd32ee6)

Pack (release export): 14 MB (12 pages x 3 mips + 3.6 MB atlas.json) -> 387 KB atlas.json, no pages
(every #1268 material is periodic, so no frame is border-blended).
Browser before-play (core+game), webpkg.py: 25.03 MB -> 10.39 MB (master 9.79). Total 204 -> 189 MB.

prepare-test.txt: dev engine tests, "owned prepared materials survive animation..." (prepares the full
catalogue), same binary, swapping only the runtime pack, interleaved x5. Old pack verified in use
(deleting a page makes the test throw). Machine load average 30-50 from other sessions.
old pack median 29.5 s (min 27.7), new pack median 27.1 s (min 22.3); new faster in all 5 pairs.

times.txt: release torus benchmark wall time; unusable (load avg 20-48, and the release pack was
swapped between runs so this is valid, but variance 20-36 s swamps any difference).
