# Documentation revamp review evidence

Tested source revision: `423242fefaabf7b6ad075be8e0096a7295aca671`. Fetched and integrated base: `978a0ea18a04f9952ae52f63064021cebc2fc926`.

Environment: macOS-26.6.2-arm64-arm-64bit-Mach-O, arm64; Python 3.14.7; Node v24.19.0. The focused Markdown parser packages were installed from `tools/docs/requirements.txt` into ignored `artifacts/docs-runtime`. Platform packages came from the committed lockfile via `npm ci --ignore-scripts --no-audit --no-fund`; Node24 was used because the host default Node20 does not meet the platform's declared engine. No native game build flags or binaries apply to these checks.

## Results

| Check | Result |
| --- | --- |
| [source-proof](source-proof.log) | PASS; 1.597s |
| [checker-tests](checker-tests.log) | PASS; 0.09s |
| [local-links](local-links.log) | PASS; 0.875s |
| [source-art-catalog](source-art-catalog.log) | PASS; 0.176s |
| [source-art-catalog-unchanged](source-art-catalog-unchanged.log) | PASS; 0.036s |
| [provenance](provenance.log) | PASS; 0.454s |
| [sim-version](sim-version.log) | PASS; 0.152s |
| [entity-random](entity-random.log) | PASS; 0.4s |
| [ci-policy-tests](ci-policy-tests.log) | PASS; 0.179s |
| [skin-materials-tests](skin-materials-tests.log) | PASS; 0.047s |
| [scene-boundary-tests](scene-boundary-tests.log) | PASS; 0.429s |
| [generator-studio-tests](generator-studio-tests.log) | PASS; 0.858s |
| [platform-format](platform-format.log) | PASS; 0.228s |
| [whitespace](whitespace.log) | PASS; 0.082s |
| [privacy-policy](privacy-policy.log) | PASS; 0.017s |
| [build-system-tests](build-system-tests.log) | LIMITATION — see below; 54.663s |
| [external-links](external-links.log) | LIMITATION — see below; 8.935s |
| [partner-portal](partner-portal.log) | PASS; 0.946s |

The deterministic checker validates **305 documents, zero errors** and inventories 68 external links. Its ten regression tests cover reference links, duplicate heading suffixes, Unicode/encoded paths, explicit anchors, images, ignored examples, category order and reachability. The 22 CI-policy, six skin-material and three scene-boundary tests pass. Generator Studio provider tests (seven cases) pass, including loading the moved authoring guide through the API module. These API tests mock the provider and require no live credentials/database.

The broader build-system suite ran 429 tests: 425 passed, one assertion failure, two errors and one skip. Two music tests abort because installed Homebrew FFmpeg references missing `libjxl.0.11.dylib`; the installed library is a different version. `test_query_and_isolated_mode` fails on macOS `/private/var/folders` versus `/var/folders` aliases. FontTools is unavailable, producing the skip. The test and helper sources for these three failures are unchanged against the base; these are explicitly recorded environment limitations, not a passing full-suite claim.

The external audit's only failure is Python's certificate-chain validation for the Microsoft Partner portal. The independent curl check uses ordinary TLS verification and follows redirects, returning HTTP200 for the same destination. This confirms reachability, not authenticated product-page access. External fragments and provider sign-in flows were not exhaustively checked. Network checks are separate from blocking CI.

## Inventory and source review

- [Baseline document dispositions](inventory.json): 300 documentation/data/notice candidates, including extensionless INSTALL/tools/README and AUTHORS. Legal/vendor/store/fixture/data exclusions remain accounted for. Every first-party authored document has a disposition.
- [Section destination mappings](section-mappings.json): 283 original section migrations; owner ledgers also record retired sections and durable extractions.
- [Lead source review](lead-ledger.md), [contributor/architecture review](contributor-ledger.md), [content/domain review](domain-ledger.md), [operations/platform review](operations-ledger.md).
- Reader journeys: [build/test/simulation/scripts](reader-journeys.md), [AI/generators/assets](domain-journeys.md), [hosting/recovery/releases](operations-journeys.md).
- [Native token and Cortex Python AST comparison](source-proof.json) and [comparison script](source-proof.py): 86 modified files differ only in comments, docstrings or documentation-path strings. No native algorithm/ABI or Cortex executable Python change is claimed.

The ledgers include intermediate review checkpoints; this summary and results.json identify the final tested revision. The generated runtime provenance inventory is corrected through its generator and verifies all 3,441 manifest frames. Original-art catalog regeneration validates both recovered archives and pre-existing sources and produces no catalog/JSON diff. Runtime assets, source artwork, simulation versions/golden records and both shipped privacy policy files remain unchanged.

## Commands and limits

[results.json](results.json) records exact commands, working directories, exit statuses, durations, tested SHA and environment. [validation-script.py](validation-script.py) preserves their orchestration; the script's Node path identifies the runtime actually used and can be substituted with another supported Node installation. [inventory-script.py](inventory-script.py) records the temporary inventory method; its owner mappings are published separately above.

Source review is targeted and accountable, not a proof of every runtime input. Supported commands were checked through safe execution/help or source inspection. Production deployment, recovery/cutover, signing and destructive procedures received static review only. No live service/provider/store changes, fresh native platform builds, device validation, gameplay measurements, performance runs, model training or cross-platform checksum campaign was performed. Engine behavior did not change, so the latter are outside the verification claimed for this editorial/tooling change. Documentation parser packages are development-only; there are no new runtime dependencies.

These artifacts live on a dedicated evidence branch, whose parent is the tested source revision. They are intentionally excluded from the documentation PR's maintained files.
