# Shore-field and countryside validation

Evidence for map generators 58 (Orchard Commons), 62 (The Comb), 65 (Portage Lakes), and 70 (The Last Treeline).

`natural5-fields.png` shows the four-map pass; `treeline7-comparison.png` and `treeline7-70.png` show the FINAL Treeline refinement after that contact sheet.

- `final-sweep.jsonl`: 400 successful generation requests (100 per map), including seeds, parameters, timings and telemetry.
- Native macOS full defaults and Comb mechanism logs, Linux toolkit log, final Treeline focused contracts, and native macOS/Linux golden snapshots are included.
- Maps: seed7, 256x256, four colonies, default controls. Each has its native map, request/report JSON and tile dump.
- Games: four mixed-AI start rotations/map, seed401/game19, 15,000 ticks. Summaries cover all16 final games; a representative native final save per map is included as gzip, with result metadata.
- `natural5-rotation-summary.json` is current for 58/62/65; use `treeline7-rotation-summary.json` for final70.

Commands: `MapGeneratorDefaultsTest .`, `MapGeneratorDefaultsTest . --treeline-only`, `MapGeneratorGoldenTest . --print`, `CombGeneratorTest . . <output-prefix>`. Generate maps with `glob2 --generate-map --generator <id> --map-seed 7 --param width=8 --param height=8 --param teams=4 --write-map true --report terrain --output-dir <dir>`. Native CLI maps and games use the recorded revisions (58:3,62:4,65:3,70:3).

Limits: AI outcomes are not human balance proof. Cortex still struggles in an Orchard rotation. Native generation was checked on macOS arm64 and Linux x86_64; Windows execution and cross-platform per-tick replay comparisons were not run. No engine, save-format or network change is included. Source was subsequently rebased over the mobile/UI merge without conflicts; PR CI covers integration.
