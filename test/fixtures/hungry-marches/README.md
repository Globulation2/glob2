# Hungry Marches verification

Run from the repository root. The scripts retain requests and results so that
failed processes, invalid requests and completed games remain distinguishable.
Freeze the executable before studying it if another build will run concurrently.

```sh
scons release=1 server=0 -j6 build/src/glob2 map-generator-defaults-test map-generator-profile-fixture map-generator-golden-test
build/src/MapGeneratorDefaultsTest hungry-contracts --hungry-marches-only
build/src/MapGeneratorGoldenTest hungry-golden --require-rows
python3 test/fixtures/hungry-marches/study_envelope.py --binary build/src/glob2 --out artifacts/hungry/envelope
python3 .agents/skills/glob2-map-design/scripts/control_study.py hungry-marches ablation --binary build/src/glob2 --out artifacts/hungry/controls
python3 .agents/skills/glob2-map-design/scripts/control_study.py hungry-marches random --teams 2-12 --count 2000 --binary build/src/glob2 --out artifacts/hungry/controls
python3 test/fixtures/hungry-marches/held_out.py --input artifacts/hungry/controls/random.jsonl --binary build/src/glob2 --out artifacts/hungry/held-out
python3 test/fixtures/hungry-marches/analyze_controls.py artifacts/hungry/controls/ablation.jsonl
python3 test/fixtures/hungry-marches/playtest.py --binary build/src/glob2 --out artifacts/hungry/games
python3 test/fixtures/hungry-marches/playtest.py --binary build/src/glob2 --out artifacts/hungry/compact --compact
python3 test/fixtures/hungry-marches/playtest.py --binary build/src/glob2 --out artifacts/hungry/duels --duel-mirror
python3 test/fixtures/hungry-marches/playtest.py --binary build/src/glob2 --out artifacts/hungry/crowded --crowded --ticks 30000
python3 test/fixtures/hungry-marches/playtest.py --binary build/src/glob2 --out artifacts/hungry/reference --reference
python3 test/fixtures/hungry-marches/analyze_games.py artifacts/hungry/games
build/src/MapGeneratorProfileFixture hungry-profile 101 40 hungry-marches --largest
build/src/MapGeneratorProfileFixture hungry-profile-observed 101 40 hungry-marches --largest --telemetry
python3 data/check_translations.py --strict
python3 test/test_translations.py -v
```

The envelope covers all nine supported shapes, every allowed colony count,
worker endpoints and three seeds, plus four explicit refusals (358 checks).
The control study measures all values, not just endpoints. Opening ration is
measured as deposits per home, concentration as central growth-potential share,
and resource controls as actual seeded tiles. Adjacent *mean* effects do not
promise monotonic geometry in every seed.

The local September 2026 evidence is indexed in
`artifacts/hungry-marches/VERIFICATION.md`: raw studies, exact game commands,
compressed logs and final saves, previews, binary hashes and performance samples.
Artifacts are intentionally outside Git. The final optimized executable reproduces
the played maps byte for byte; the late large-layout radius fix was separately
retested. Do not count abandoned prototypes or incomplete disk-full runs as games.

Completed games show food collection, population growth and combat, not proof of
human balance or of a causal raiding advantage. Maxima can stall on dry starts
because its birth budget discounts finite food. Nicowar's existing enemy iterator
fails with twelve occupied team slots; crowded tests use Cortex/Cabino/Maxima.
These AI limitations are recorded rather than patched in an additive map change.
Cross-platform generation checks and human games remain unperformed.

During integration onto master, the catalog ID moved from65 to69 because
Portage Lakes had already taken65. Historical frozen binaries and their artifacts
retain65; current commands use the stable name `hungry-marches` or ID69.
