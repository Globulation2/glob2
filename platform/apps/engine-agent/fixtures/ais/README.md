# AI validation fixtures, suite 1

These script-free maps are frozen outputs of the symmetric-arena generator
(method 15), one candidate, default settings except:

| File | Seed | Width/height powers | Teams |
| --- | --- | --- | --- |
| two.map.gz | 19 | 6 / 6 | 2 |
| four.map.gz | 73 | 7 / 7 | 4 |

`manifest.json` pins the bytes, player count, and gameplay seed. Validation checks
their hashes before advertising its job capability. Do not regenerate fixtures
without incrementing `AI_VALIDATION_SUITE`; historical reports retain their suite.
The runner first freezes a current-format initial save, then runs, repeats, and
resumes that state so legacy map-header upgrades do not affect comparisons.
