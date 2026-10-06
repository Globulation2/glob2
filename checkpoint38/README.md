# Building capability review and polish — checkpoint38

Tested final source: `5f6f31e55e9772ea14c4b4d3cf6a701bef418dac`. Integrated master/base: `cbf71a464969ceda7a786696f89ae66ea7cf52ff`.
The preceding cleanup commit is `9c6fd848412317c5dc5615404e1590925942c63f`; its initially failing CLI example and a failing regression against that executable are retained.

Two fresh reviewers examined catalog architecture, authoring documentation and tests, then cross-reviewed the changes. The work adds contextual loader errors, explicit ownership/compatibility comments, checked AI mask assumptions, a complete experimental-building example, named rejection cases and a production CLI regression. The example exposed a real setup bug: an already-consumed catalog path was parsed again as a numeric generator control. The final two-line fix skips that duplicate interpretation. No formerly valid simulation changes, historical RNG scaffolding or hot-loop descriptor changes were introduced; simulation revision 20 remains unchanged.

## Final verification

- GCC13.4.0, Linux x86-64, release `-O3`: production client and both native test binaries built successfully from a clean tree. Exact flags, toolchain, source hash and binary hashes: [build provenance](build-provenance.json). Build commands and linked dependency hashes are in the archive.
- **61/61** focused engine cases pass, zero skips: BuildingCatalog, BuildingCapabilities, AICustomCatalog, BuildingCatalogFixtures, AIRules and AIStateContinuation. [JUnit](results.xml); inventory and full logs in archive.
- **9/9** real production CLI contracts pass. The new test forwards a custom catalog through generated-map setup using a path containing spaces, removes the installed catalog, reopens the embedded map, and compares all 64 tick records. Its saved continuation preserves the gate and matches all 32 remaining tick records. [CLI JUnit](cli-results.xml).
- Native JavaScript unit/engine corpus passes. Full raw outputs, traces and manifest retained.
- Committed match verification passes. The 1500-tick native reference trace and committed-match trace are byte-identical to checkpoint37. [Comparison hashes](reference-parity.json).
- The exact authoring-guide shell block passes: stock catalog plus experimental feeding/healing kitchen, generated map, Numbi/Castor, 512 ticks, initial/final saves, enabled `field-kitchens`. Commands and files retained.
- New regression was observed failing with `stoi` on the preceding executable, then passing with the fix. Initial failures are retained, not relabeled.

No performance benchmark or browser/platform rerun was performed for this bounded cleanup. The [checkpoint37 evidence](https://github.com/Globulation2/glob2/blob/72ce59df82e474be8060d8424cc5024e4ca24e30/checkpoint37/README.md) remains separately scoped: prior performance failures/inconclusive endpoints, Firefox rendering failures, browser harness/mode gaps and unavailable native platforms are unchanged. Human gameplay review remains open. This PR remains draft and does not meet the overall acceptance gates.

## Inspect and reproduce

[Review dispositions](reviews.json), [final command results](summary.json), [archive inventory](files.json), [integrity verification](archive-integrity.json).

Download the parts named in [archive metadata](archive.json) and verify [SHA256SUMS](SHA256SUMS), then concatenate/extract:

```sh
sha256sum -c SHA256SUMS
cat evidence.tar.zst.part* > evidence.tar.zst
zstd -d -c evidence.tar.zst | tar -xf -
```

The archive contains `polish38/` with the first build/test/failure and `polish38/final/` with final-source validation. Each stage records commands and provenance. `final/validate.py` documents the complete native verification sequence; `final/documented-example.sh` is extracted verbatim from the guide. Retained inputs, saves, replays and component traces support direct inspection. Disposable profile directories are intentionally excluded; the original failed CLI metadata and logs remain included. Every published archive member was decompressed and checked against its SHA-256 inventory.
