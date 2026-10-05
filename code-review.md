# Code review: player discovery, AI profiles, profile photos

Scope: uncommitted implementation, inspected by a separate code-review agent in two rounds. Reviewed API/query behavior, account and blob lifecycle, additive protocol contracts, native compatibility implications, and related test coverage. UI design reviewed separately.

## Round one findings and resolution

- Gravatar refresh held an account row lock and a pooled database connection throughout up to three seconds of upstream work. A directory page of cold avatars could exhaust the default ten-connection pool. Extracted the refresh service: image downloads, normalization and storage now run outside database transactions; upstream work is limited to four concurrent refreshes and 128 pending account/revision/fingerprint requests per replica. Conditional writes discard a refresh if preferences, identities, account status or the previous cache changed.
- Linking, unlinking and provider-email changes invalidated Gravatar only when the server received another image GET. Mounted images, especially cached failures, retained an unchanged URL. Identity changes now clear cached Gravatar state and increment the avatar URL revision; old bytes become inaccessible immediately and are reclaimed by the existing orphan GC grace period. Uploaded-photo/initials preferences remain independent of identity changes.
- AI match-history queries omitted the explicit participant-kind predicate required by their partial index. Added `p.kind = 'ai'` predicates.
- Directory offset pagination eventually returned a cursor beyond its hard cap. Replaced it with bounded keyset pagination using relevance, normalized name, participant kind and ID; the seek is pushed into the indexed human branch.
- Profile ranks performed one whole-ranking query per rating row. Shared ranking helpers now batch all ladder ranks in one query and preserve the leaderboard ordering.
- Hash ordering cannot establish simulation revision chronology. The default now uses numeric version fields and agent introduction timestamps for supported builds, with deterministic hash tie-breaking. Documentation explains that same-number timestamps are a practical deployment-order fallback, not semantic SIM_REVISION metadata.
- Avatar URL formatting originally coupled authentication to the full history module. Extracted a small shared URL helper, and separated Sharp validation and Gravatar retrieval from HTTP routes.

## Round two

Re-read the integrated directory keyset SQL, bounded branches, cursor validation, AI index predicates, supported-version selection, ranking window partitions and overall-rank selection. No additional blocking correctness findings. The Gravatar service was corrected to avoid TypeScript parameter properties, preserving the production Node strip-only execution mode.

Focused verification: `npm test -- apps/api/test/avatars.test.ts apps/api/test/identity.test.ts --maxWorkers=2` passed 28 tests (11 avatar, 17 identity). New regressions cover linked-email URL invalidation, unchanged sign-in, concurrent opt-out and linking during refresh, discarded blob cleanup, database connection release during upstream waits, bounded concurrency, request coalescing, and an actual AbortSignal timeout. Existing validation, EXIF/metadata normalization, authentication/CSRF, Gravatar lookup/cache, preference and deletion checks also passed.

Limitations: native OnlineResources compatibility has not been run by this reviewer; the main agent subsequently resolved the dependency path and passed all five OnlineResources cases; see native-online-resources.log. Hash-only simulation identities still provide no authoritative semantic revision chronology. Identity-invalidated blob reclamation uses the existing seven-day orphan grace period; the image is immediately inaccessible. At exceptional queue saturation, avatars fall back to an existing photo or initials and retry on a later visit. Final integrated suite/build/browser results are recorded separately by the main agent.
