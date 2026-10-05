# PR #220 review working notes

The PR changes approved source artwork and offline Python tooling. It contains
no renderer, simulation, save, replay or network code changes against the rebased
master. The existing renderer and shared runtime exporter retain ownership of
runtime image lookup, team recoloring, atlas admission and WebP encoding.

Resolved review findings:

- The previous instructions called a PNG source assembly a runtime build. The
  assembly command now distinguishes source output and optionally delegates a
  complete WebP tree to tools/package_assets.py. It does not add another encoder.
- Production and candidate validation used Python assertions; validation remains
  active with -O. Validation checks the complete selection before source writes.
- Reusing candidate directories could mix old and new layers/recipe records, and
  failed inference could publish partial base/team results. Candidates now require
  fresh staging and publish one complete selection only after all layers succeed.
- The previous output protection permitted writing the repository root or other
  tracked directories. Staging is now external or below ignored artifacts/, with
  resolved-path checks. Source assembly rejects stale and symlinked output files.
- Candidate model-dimension errors now fail explicitly. Recipes retain source,
  executable and model hashes plus image-tool versions for review evidence.
- The source validator ran on import, repeated image reads, and mixed all checks
  into global code. It now has isolated frame/index, resource, terrain, water and
  decoded-runtime validation functions with a CLI boundary.
- Source PNG hashes cannot describe encoded WebP. Runtime verification checks the
  adjacent export audit, actual WEBP decoding, shared policy, geometry, exact alpha
  and exact RGBA for lossless selections, plus the rewritten frame index.
- The old completeness assertion incorrectly included newer experimental terrain.
  Those frames use the shared compiler/native fallback; HD connected terrain
  validation explicitly covers all 272 legacy connected tiles and their mip joins.
- The generated provenance document used broken relative source links after its
  move into docs/assets/high-resolution/. The generator and maintained inventory
  now use the correct repository-relative depth.

PNG artwork is byte-identical to the previous PR wherever files overlap; master
adds unrelated source assets through the rebase. This update changes tooling and
runtime encoding integration, not the reviewed AI selection or finishing recipe.

External Real-ESRGAN inference is covered by a deterministic fake adapter in
failure tests; real model quality/reproducibility is not claimed. Human gameplay
and performance acceptance remain a maintainer review activity. Native local
coverage is macOS arm64; report other runtime/platform checks actually completed,
not inferred from shared code. The full build-system attempt also retains its
unrelated FFmpeg shared-library failures and the fontTools skip.
