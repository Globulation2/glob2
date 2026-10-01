# Evidence: wizard unit concept

Review material for the draft pull request that proposes a magic-casting ranged
unit, the "wizard". It is not meant to be merged. The tooling is in
`tools/unit-animation/` on branch `wizard-unit-concept` (commit 551dc2843).

| Path | What it is |
| --- | --- |
| `previews/compare.webp` | Wizard walk and cast next to the shipped worker and warrior, 128 px, front and side |
| `previews/wizard-{walk,swim,cast}-hd.webp` | Each set in all 8 directions, 32 poses, 128 px |
| `previews/teams.webp` | The engine's team hue shift at six hues |
| `previews/native.webp`, `previews/native-3x.webp` | Every set at the in-game 40 px (1× and 3×); the right column is the shipped warrior |
| `previews/swim-before-after.webp` | Shipped warrior swim, the first wizard swim (reusing the warrior's stroke) and the rebuilt wizard swim, 40 px at 3× |
| `history/` | Design iterations, from the rejected hat versions to the final pointed oval |
| `wizard-preview.html` | The full illustrated write-up shown to the maintainer, self-contained |
| `sources/wizard-*.blend` | Generated Blender 2.34 sources (`derive_unit.py build`) |
| `sprites/wizard-sprites-{40,128}px.zip` | Every rendered frame: team layer 0004–0259 and, for walk and cast, the shadow layer 0260–0515 |

To regenerate, from the repository root on `wizard-unit-concept`:

```sh
python3 tools/unit-animation/derive_unit.py build  --work WORK
python3 tools/unit-animation/derive_unit.py render --work WORK --size 40
python3 tools/unit-animation/derive_unit.py render --work WORK --size 128
python3 tools/unit-animation/preview_unit.py --work WORK --out OUT
```

`WORK` must be mounted in the Blender 2.34 container at `/work` (see
`tools/unit-animation/Dockerfile.blender234`). Sources rebuilt by the committed
tool render byte-identically to the archived sprites: 18 sampled frames across
the three sets were compared.
