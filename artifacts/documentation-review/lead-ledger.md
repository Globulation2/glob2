# Lead integration and factual review

Temporary evidence for the documentation revamp; not a maintained guide.

## Ownership and inventory

The baseline is 3f67899b0899780efb4c309d111852b1e9f267ff. The baseline inventory includes every tracked md/txt/rst candidate, root/colocated documents, the extensionless INSTALL/tools/README, AUTHORS attribution and Debian READMEs. inventory.json records each candidate's disposition, destination, exclusions and section mappings. Legal texts, store metadata, runtime data, fixtures and upstream documentation retain source ownership; excluded candidates are accounted for rather than silently dropped. Owner ledgers give extraction/disposition and source review for their topic sections. New pages are linked from topic indexes; navigation.json and the deterministic checker enforce that topology.

## Lead-owned sections and sources

| Document/section family | Factual sources and review |
| --- | --- |
| README: purpose, reader entry points, public gameplay/site relationship | Current repository tree, existing public site references; actual GitHub origin and public website repository. External-link audit checks linked pages. |
| CONTRIBUTING: build/test workflow, compatibility, PR/evidence/review | AGENTS.md policies, tools/dev_build.py, existing test runner and CI selector. Preserve actual policy; do not invent a separate approval requirement. |
| SUPPORT: issues and help | GitHub repository hasIssuesEnabled=true; existing public guides. No new security contact, response guarantee or governance policy. |
| docs/README and docs/tools/README | All topic indexes, tools tree and source-owned colocated guides; final graph reachability check and reader journeys. |
| documentation maintenance: canonical ownership, page structures, generation, source verification, evidence | User's accepted editorial plan and AGENTS.md documentation/evidence policies; checker implementation and tests match described command/exception behavior. |
| Licensing navigation | Root COPYING; existing library/license notices and asset provenance. Navigation only; no legal text rewritten. |
| AGENTS repository map | Current src domains and new canonical destinations. Diff reviewed for preserved compatibility, approval, evidence and merge policies; CLAUDE and skill symlinks retained. |
| tools/README | tools tree and tools/SConscript packaging; retain externally packaged extensionless path. |
| INSTALL | SConstruct Linux install aliases and options; building.md owns detailed install/data-path contract. Packaged extensionless path preserved. No installer executed. |
| Debian READMEs | debian/rules, debian/source/format, current SCons SDL3/dependency policy; remove obsolete compiler minimum prose. Debian packages were not built. |
| Lobby automation | Existing integration-test/run_lobby.py and test registry; moved guide retains command contracts and narrowed purpose. No live lobby driven. |
| Tournament references/examples | tools/tournaments/experiments.py and worker.py argument parsers/defaults; safe --help checks. Remove private host names and local paths; examples use placeholders. No distributed tournament/performance claims. |
| Source/workflow/skill pointers | Combined destination mappings plus repository-wide old-path scan. Source tokens and Cortex Python ASTs verify comments/docstrings and documentation-path strings only. Generator Studio API's authoring-guide readFileSync path and web documentation URL updated; focused provider tests cover API module loading. |
| Late corrections | Current EngineTiming default30 and timing tests; scripting CLI JavaScript mode source; dataset writer format documented by headless-replays; ConnectionOverlay Slow overrides metric to Poor. |

## Tooling and generated material

- MarkdownIt is a pinned development dependency only. The checker operates on tracked first-party Markdown plus named authored extensionless guides; generated/legal exceptions skip structure rules but still check their links.
- Regression tests exercise reference links, duplicate heading suffix collisions, Unicode headings, encoded paths, explicit anchors, images, fenced/explicit ignored examples, reachability, category ordering and missing main index. CI selector tests cover cheap classification. No network audit blocks CI.
- Runtime provenance generator was corrected to classify the existing procedural resource painter recipe. Regenerated inventory now covers all 3,441 manifest frames. Runtime assets, source artwork and game manifests remain unchanged.
- Original-art catalog regeneration verifies preserved bytes for both archives plus pre-existing sources and must produce an unchanged catalog/JSON inventory.

## Coverage limits

Source review was targeted and section-accountable; it is not an exhaustive proof of every possible runtime behavior. Commands were checked by safe help/execution or source inspection. Production, recovery, signing and destructive procedures received static review only. No live infrastructure, provider credentials, stores, devices, fresh native platform builds, gameplay, performance or cross-platform checksum campaign was exercised. Native algorithm/ABI changes were excluded by token review; Cortex Python changes were excluded by AST comparison. Focused documentation, CI-policy, provenance and API checks are recorded separately from the broader build-system suite's known environment failures. External GET checks do not verify every external fragment or authentication flow.
