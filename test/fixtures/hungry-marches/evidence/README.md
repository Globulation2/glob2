# Hungry Marches evidence

This is a portable subset of the complete local evidence indexed at
`artifacts/hungry-marches/VERIFICATION.md`. Source commit and upstream integration
base are in `integration.json`. Historical study/game binaries used numeric ID65;
the merged generator uses ID69 because Portage Lakes took65 first. The final
integration changes registration, not Hungry Marches geometry or simulation.

- `integration-*.log`: successful full contracts,536 golden rows, strict
  translations and5 translation tests after rebasing onto current master.
- `ablation.jsonl.gz`, `random.jsonl.gz`, `held-out.jsonl.gz`: every request,
  outcome, metric and telemetry record from the493/2000/1000-request studies.
  Redundant nested generation/quality reports were removed; no rows were removed.
  Random requests include516 expected unsupported cases; held-out includes255.
- `large-layout-checks.jsonl.gz`:703 passed requests after the late116-tile radius
  cap, including every affected sampled request and dense largest-map probes.
- `envelope.jsonl.gz`:358 final checks, all passed, including4 expected refusals.
- `control-effects.json`: each adjacent mean control effect increases under its
  intended metric. Ration is exact stock; wheat density is not eventual capacity.
- `completed-games.json.gz`: outcomes and exact commands for28 completed games.
  The four economy files contain harvested/delivered food and timber, population
  peaks and combat/starvation counters. Original absolute paths identify provenance;
  use the parent fixture scripts to reproduce them on a different checkout.
- `example.map`, `example-final.game.gz`, `example-run.log.gz`: seed404 rotation3,
  four colonies, game seed19,45000-tick cap, Nicowar/Cortex/Cabino/Maxima. The save
  was successfully loaded for the late-game preview. Decompress before opening.
- `played-map-identity.json`: all28 played map files match the final optimized
  pre-integration generator byte for byte. `optimization-identity.json`:24 varied
  requests match the same-geometry pre-optimization build.
- `quiet-before.txt`, `quiet-after.txt`:512²/12 colonies,seeds101–140;40/40 success
  both builds, mean541.256→139.754ms. Shared-host sequential measurements.
- `large-106.png`: reviewed final large-layout regression preview.

Repeated source/visual agent reviews informed revisions; a separate translation
agent reviewed catalogs twice. These are the author's review assistants, not
independent maintainer approvals. No human balance testing or Linux/Windows
execution comparison was performed. Automated games show economic activity and
combat, not proof of equal win rates or that raiding causes wins.

Existing AI limits: Maxima can stall on dry finite-food starts; Nicowar's Echo
iterator overruns a fully occupied12-team roster. Crowded completed games use
Cortex/Cabino/Maxima. The failed Nicowar attempts and disk-full prototype runs are
excluded. No AI or simulation changes are included in this map addition.
