Non-market buildings hire and route workers to their team's stocked markets, including when the map has none of the requested resource. Markets replenish from natural resources, avoiding circulation between markets. This PR is independent of #254: it preserves master's worker attachment and next-delivery behavior, including when most workers are idle.

Market tiles carry a five-tile detour in both resource and round-trip fields. Workers take stock at the door and carry it to their employer; a nearer natural resource can still win. Retained workers select their next delivery with the same market-aware fields. The legacy in-flight exchange-building arrival path remains readable for existing saves.

Plain and market fields refresh as separate scheduled fields using the current lazy-allocation lock, propagation workspaces, one-field-per-tick round robin and optional fixed-delay gradient pipeline. Stock transitions invalidate pending snapshots and request a refresh. Colonies without markets retain the original resource fields and refresh schedule.

Save format 134 stores market fields, scheduling flags and pending publications. Older saves remain readable with floor 58 and allocate market fields on first use. Existing pending-gradient destination IDs are preserved. SIM_REVISION is 16, distinct from prior stacked builds; the golden match record and trace are regenerated. Replay floor 127 and protocol 54 remain unchanged; simulation-version keys isolate differing builds.

The branch starts from master `cd6ab247ac0feb48a5c90000ae39b3c3060e9891` and includes only the market-fetching work. #254 remains a separate hiring proposal with its unresolved balance review. #258 builds market upgrades on this PR.

Current validation and accessible evidence are recorded in the separation comment. Cross-platform per-tick comparison and human market gameplay remain pending. This changes market depot use and delivery route choice; a maintainer playtest is still needed, so the PR remains draft.
