# Generated-map rotation offset regression

Switchbacks seed `4143377922` at 128×128, two teams and four workers used to
produce valid maps but fail rotation export (`artifact_failure`, exit 6)
on the Maxima branch, whose continuation save format serializes the call lists.
The first load initializes the towers' resource call lists. This grows the
serialized team section, moving the map offset by 12 bytes. `Game::save` hashes
its initial header before patching that offset; the first canonical save's SHA1
therefore differs from the next save, even though loaded game state is stable.

The export now saves the same loaded object again when that first save moved the
offset. Subsequent reload and full-rotation comparisons remain strict. The
loader, generators, simulation, save format and version gates are unchanged.
There is no gameplay or visual change.

Run all main-binary CLI regressions (including the three tournament failures):

```sh
scons release=1 server=0
python3 test/tournament_cli_integration.py --output /tmp/rotation-cli-check
```

The attached compressed maps retain Switchbacks rotation 0 before/after and
rotation 1 after the fix. Only rotation 0's 20-byte header SHA1 changes; rotation 1
is byte-identical before/after. `evidence.json` records commands, binary hashes,
exit codes and map comparisons for Switchbacks, Amphitheatre and Carousel.
`cli-verification.json` records the full CLI integration cases. These are macOS
arm64 checks, not a claim of cross-platform simulation equivalence.
