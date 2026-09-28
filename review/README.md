# Post-review validation

Source commit: `8efd999b9e07759a4140138e4cb7d78f0134d9b5`. The wood reserve now rejects external forbidden outlets and keeps its own farm protection from disconnecting reserve reachability. All four focused Maxima regression suites passed.

The same reproduction commands and inputs in the parent README were rerun on macOS/ARM64 and devlaptop Linux/x86-64. Complete checksum sidecar hashes match across both platforms in all four cases; detailed tick hashes are retained here. Arena and Rice differ from the earlier production commit, while self-play and the older-save case remain identical. The large tournament was not rerun after these fixes, and Windows simulation equivalence remains unverified.
