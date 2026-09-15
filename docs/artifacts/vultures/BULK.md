# Vultures: bulk generation and rectangular tuning — 2026-09-15

[Design and tuning rationale](../../map-generators/VULTURES.md) · [Raw bulk evidence](https://github.com/Globulation2/glob2/blob/evidence/vultures/docs/artifacts/vultures/bulk-evidence.zip) · [Reproduction runner](https://github.com/Globulation2/glob2/blob/evidence/vultures/docs/artifacts/vultures/bulk-runner.py)

**Final result:** revision 2 generated 406 of 461 requested maps. The other 55 were
explicit validation rejections for colony counts that cannot fit the shared pond,
swarm, clearing and intervening field geometry. There were no generation failures,
crashes, runner errors, timeouts, missing map reports, dropped telemetry records or
invalid telemetry values. All 268 parameter stress requests generated successfully.

## Workload and classification

The first sweep covers every combination of 64, 128, 256 and 512 tiles on each
axis with one through twelve colonies, map seed 1001, default settings and no
candidate selection. That is 192 unique geometry requests plus one baseline
duplicate, for 193 jobs. It includes both rectangle orientations and the smallest
64×64 map. A rejection is counted separately from a generation failure: it occurs
at the `validation` stage, carries `invalid_request`, and says to use a larger
map or fewer colonies. Successful reports reach `complete`.

The second sweep has 268 jobs. It probes every discrete value of Home size
16–30, Lakes 0–4, Lake size 40–160 in steps of ten, Wheat/Wood amount 0–200
in steps of 25, and starting Workers 1–8. One-factor probes use 256×256 and
four colonies so Home size actually changes the layout. It also runs both
orientations of all 16 dimension pairs at conservative legal colony counts,
extreme small-map combinations (eight workers, maximum home and lake controls,
zero ambient food and wood), and 48 seeded mixed-control configurations, mostly
with two seeds. The exact case list, including baselines and seeds, is in the
archive; these counts describe requests, not independent matches.

| Sweep | Revision 1 complete | Revision 1 geometry reject | Revision 2 complete | Revision 2 geometry reject |
| --- | ---: | ---: | ---: | ---: |
| Dimensions × colonies | 132 | 61 | 138 | 55 |
| Full controls and mixed corners | 266 | 2 | 268 | 0 |
| **All jobs** | **398** | **63** | **406** | **55** |

Every revision-1 completion remained a completion in revision 2. Eight
previously rejected jobs completed; there were no reverse transitions. The
eight successful fallbacks each recorded `vultures.homes.vacancies` telemetry.
The final Linux binary was built in the isolated snapshot, updated to the
current master map-report source, on
`devlaptop.local` with `scons -j8 release=1 server=0 build/src/glob2`;
its SHA-256 is
`220b81100a1dedb72cb8a169a3b5dca13e556ec2b70588f557d5570526b7881d`.
The five directly changed generator source files and four current-master
map-report source files match the workspace hashes
in `bulk-source-sha256.json`. The first sweep used the immutable revision-1
bundle `27e7c8d3e5e8a1025c694f8fabfda17a65a0eeba25ab81376b11b7e815476ac6`
on idle `devlaptop.local` and `pharaoh-dev-1.local`; the parameter sweep used
idle `devlaptop.local`, `pharaoh-dev-2.local` and `pharaoh-dev-3.local`.
`therig.local` was occupied, so it was not used.

## Small and rectangular maps

The table gives the highest colony count that generated at default settings
and seed 1001. Final legal counts in each cell were contiguous from one to
that maximum; the raw reports provide every attempted count.

| Width ↓ / Height → | 64 | 128 | 256 | 512 |
| --- | ---: | ---: | ---: | ---: |
| 64 | 1 | 2 | 6 | 12 |
| 128 | 2 | 5 | 12 | 12 |
| 256 | 5 | 10 | 12 | 12 |
| 512 | 10 | 12 | 12 | 12 |

The original equal-row lattice forced prime counts into a long row on some
rectangles. A 256×128 map with seven colonies was rejected twice in the mixed
sweep even though eight fit; 128×256 with eleven was rejected while twelve
fit. The shared, opt-in vacancy lattice can start from a better composite
grid and leave up to four sites empty. It evaluates the minimum wrapped
whole-tile separation after each omission, keeps a deterministic first
choice on ties, and accepts the new positions only if the existing home-room
test passes. The seven-colony mixed case now has spacing 64 and effective
home radius 16, with one vacancy recorded. No passing historical layout
is replaced, and Old Growth never opts into this fallback.

Compact shapes still reject incompatible colony counts honestly. For
example, a 64×64 map generates with one colony but cannot hold two separate
pond-and-swarm clearings with the required dry field between them. The
control ranges remain unchanged; no setting was narrowed to conceal a
failed request. All generated maps passed the finished-world audit for
walkable inter-colony routes, dry one-harvest wheat, shoreline-only wood,
starter stock, opening supply access and building room.

## Evidence and reproduction

`bulk-evidence.zip` includes both planned workloads, revision-1 and
revision-2 raw JSONL results and map reports, the exact 461-case replay
list, summary counts, a source/binary hash manifest, and the final
seven-colony rectangular `.map` and report. Reports include all generator
telemetry; the status field separates a clean invalid request from any
runner or binary failure. Its SHA-256 is in `bulk-evidence.zip.sha256`.

After extracting the archive on a machine with a built `glob2`, rerun the
final requests with:

```sh
python3 bulk-runner.py ./build/src/glob2 r2-cases.json replay-results.jsonl --jobs 4
```

The runner converts width/height exponents to tile sizes, gives each process
an isolated profile, retains every JSON report, and times out after 180
seconds per case. Four workers used less than half the idle 16-core Linux
host; worker count is adjustable and does not affect the map controls.
MacOS ARM64 passed the shared toolkit/default checks and all 256 golden
rows. The eight Vultures golden map fingerprints for previously legal
requests were unchanged when their revision was updated from one to two.
For the rescued 256×128 seven-colony seed-5001 case, generating to the same
output path with and without `--json` produced byte-identical serialized
maps (SHA-256
`a972b97abb91b262ce53b53300dc79576426a971a69ad51084478810ffc8f3ea`).
The output basename is serialized as map metadata, so comparisons must use
the same output path; two different filenames have different full-file hashes.
The [AI playtest](PLAYTEST.md) uses those unchanged revision-1 maps and a
same-serialized-map Linux/macOS checksum comparison. Independent same-seed
generation can differ between these platforms even for Old Growth, so the
bulk job does not claim byte-identical map generation across platforms.
Human play and Windows execution were not included.
