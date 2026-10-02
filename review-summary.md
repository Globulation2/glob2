# Independent review feedback

All reported defects have been addressed and follow-up reviewed.

- P1: Require a browser output ownership marker before replacing its directory. A generic website containing index.html must be preserved. Added package.json, staged integrity checks, rollback, and regression tests.
- P2: Require every HTML/JS/WASM/data gzip sidecar, even if the missing file is removed from SHA256SUMS. Verification now enforces the exact package file set.
- P2: Remove shipped legacy PNGs at managed image paths when upgrading to WebP, including older changed artwork, so PNG precedence cannot mask the new image. Per-user overrides retain priority; migration behavior is documented.
- Architecture: Fingerprint only pkg-config linked codec/SDL libraries, resolving aliases and hashing each real file once. Unrelated Homebrew upgrades no longer invalidate the lean codec cache.
- Quality: Normalize new Python helper formatting, remove unused imports, centralize encoder pin probing, and document ownership, lease, cache, and upgrade policies. Keep one shared exporter rather than duplicating format selection in platform packagers.

Follow-up review found no introduced correctness defects. Platform integrations were inspected, not independently built by the reviewer. The Mac cache fingerprint covers directly linked libraries rather than every header or transitive build input.
