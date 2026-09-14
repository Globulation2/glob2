---
name: glob2-map-design
description: Design, implement, tune, or review Globulation 2 procedural map generators, translating a map concept into playable terrain, sustainable colonies, fair routes, varied layouts, shared primitives, and registered controls. Use for generator work in this repository, rather than ordinary map editing or unrelated simulation changes.
---

# From map concept to playable generator

Treat a map as an economy and a travel network that evolve during play. Its silhouette is only one part of the design. Preserve the user's concept while finding geometry, resource policy, and controls that make its intended decisions possible.

This skill is grounded in a code/comment review of all 31 built-in generators. The references distinguish current mechanisms from design recommendations; they do not certify that every existing generator is fun or balanced. Paths below are relative to this skill. Follow repository `AGENTS.md`; recheck relevant code when implementing because this guide is a source map, not a frozen engine specification.

## Turn the idea into a playable contract

Write a short design brief before coding, scaled to the request:

- What choice should players face: early pressure, contested farmland, several fronts, compact city expansion, clearing a forest, or investing in swimming? Where and why do opponents meet?
- What does every colony need to establish food, wood, population, upgrades, and an army? Budget connected building footprints, worker access, renewable crops, and exits separately.
- What is home ground, what is expansion ground, and what is worth contesting? Describe the opening, first contact, and later changes as resources spread or units gain abilities.
- Which properties are invariants, which may vary by seed, and which are player controls? State the supported sizes, aspect ratios, and colony counts. Make unsupported combinations explicit.
- Give topology words a scope: does “two crossings” mean globally, per colony, or per neighbour? Does “one entrance” remain true after swimming? Resolve consequential ambiguity before encoding the wrong route graph.
- How will the design fail in practice, and what observation would cause a revision? Examples: a crop belt engulfs homes, a tower covers the only attack route, swimming removes the intended front, or equal starts lead to radically unequal expansion.

Choose the construction family by the play contract. Recognizable shapes can use transformed masks and repeated home modules; landscapes can use fields, regions, and scored start placement; arenas can carve connected rooms and fronts from impassable ground. Hybrids are useful. Do not force an asymmetric landscape into exact symmetry merely because it is easier to validate.

## Read the references that bear on the design

- Read [gameplay and playability](references/gameplay-and-playability.md) when deciding terrain, resource sustainability, AI access, building room, fighting distances, or playtests. It explains why initially open maps can become unplayable.
- Read [shaped generators](references/shaped-generators.md) for arenas, cities, canals, repeated modules, rotational/translation symmetry, and variation inside a defined shape.
- Read [landscape generators](references/landscape-generators.md) for organic terrain, asymmetric starts, hydrology, resource bands, forests, and the older generators' useful lessons and limitations.
- Read [implementation and verification](references/implementation-and-verification.md) when selecting primitives, adding controls, registering code, repairing helpers, or planning evidence. It includes commands and explains what each check does and does not establish.

Use the generator tables to select the closest precedents, then read their current headers, complete implementations, and relevant helper bodies. Compare the intended behavior in comments with what the final validator actually checks. Reuse a mechanism, not an entire generator's accidental constraints.

## Design and implementation loop

1. **Budget geometry before decoration.** Fit homes, complete building footprints and upgrade room, sustainable crop plots, gathering edges, multiple useful routes where intended, beaches, and boundary thickness on the smallest supported map. Use toroidal distances and account for rasterization. A large grass count does not imply buildable space.
2. **Control the future resource footprint.** Separate guaranteed starter supplies, renewable farmland, ambient abundance, and structural deposits. Use water placement, dry zones and sand containment deliberately. Protect roads and expansion ground. The engine also has a `canResourcesGrow` tile flag, but current generators do not establish a shared no-growth-region pattern; using it needs explicit final-world and save/load checks. More wheat is not automatically better for an AI.
3. **Choose an honest fairness model.** Exact tile orbits can prove symmetry; independently rasterized wedges cannot. For asymmetric worlds, optimize actual post-settlement resource access and room, then inspect expansion and contact costs. Randomly deal sites to team indices before indexing kits or defenses. Equal starting scores are useful evidence, not proof of fair games.
4. **Compose shared stages.** Keep the layout in a reconstructible `design(request, context)`, stamp terrain, settle colonies, furnish resources, repair only permitted failures, and validate the finished world. Inspect `shared/` before inventing a helper; extract a reusable routine on its second use. Fix primitive bugs with regressions and account for every affected generator's output.
5. **Make variation strategic.** Change routes, field locations, district/cell shapes, crossings or objectives within the concept's invariants, using named seed streams. Mere resource sprite changes or shuffling team labels are insufficient replay variety. For exact symmetry, vary a fundamental domain or an invariant field and reproduce its orbits.
6. **Expose meaningful variants.** Controls should change a player decision, such as crossing width or room allocation. Wire every ambient resource layer to its applicable registered resource amount. Document guaranteed supplies and structural-wall exceptions. Test both scarcity and crowding: 0% may retain the starter guarantee, while high amounts must leave room and routes.
7. **Instrument and analyze internal decisions.** Use the [telemetry guide](../../../docs/map-generators/TELEMETRY.md) to record effective variants, requested/placed features, calibration inputs/outputs and actual fallback or omission branches. Reuse existing values and cheap counters; collection stays off outside requested JSON output, and telemetry must add no RNG draws, grid scans or expensive analysis. Keep stable, bounded metrics that explain many seeds; remove temporary per-tile/per-candidate debug traces. Bulk-generate a seed/settings matrix and join internal observations with final-map metrics, inspecting failures, missing records and rare variants before tuning. Benchmark generation with collection on and off.
8. **Verify progressively and revise.** First check structural invariants and final geometry, then seed/parameter sweeps and preview comparisons, then real AI games and human play. Include late growth, expansion and alternate movement abilities. Tune on one seed set and check another; retain failing seeds. Revisit the design when a repair erases its identity.

## Completion evidence

For generator work, report the intended play experience, changed controls/primitives, supported parameter envelope, reproducible commands and artifact locations, and observed limits. Include maps with request JSON, previews, relevant sweep results, internal telemetry and its bulk analysis, generation timing comparisons, and game evidence proportional to the change. Distinguish generation repeatability, static fairness, AI performance and human fun. Follow the repository's compatibility and review rules for changes extending into simulation or existing gameplay; do not claim unperformed platform comparisons or playtests.
