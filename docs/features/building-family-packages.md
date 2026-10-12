# Building family packages

Companion to [building catalogs](building-catalogs.md).

## Portable building families

The `BuildingPackage` contract in `platform/packages/protocol/src/buildings.ts`
represents an additive family, including its construction, repair and upgrade
variants. A package has `schemaVersion: 1`, a lowercase UUID `namespace`, and
`variants`, `experiments` and `sprites` arrays. Variant, experiment and connection
group keys use the prefix `b-<namespace>-`. Variant references and experiment
requirements stay within the package; packages cannot replace stock definitions
or change the base catalog's starting building. Runtime `id` fields are rejected.
The platform's fork helper assigns a new namespace and rewrites those references.
Every package variant needs a `properties.gameSprite`. It also needs
`properties.miniSprite` unless `miniSpriteImage` is explicitly `-1` to suppress
that image. Omitting these paths leaves unusable engine defaults and is rejected
during native validation.

Compose authored manifests with the capability-advertised command:

```sh
glob2 assets compose-buildings --format json --package first-family.json --package second-family.json
```

An optional `--base PATH` selects a base catalog manifest.
`--artwork-bundle PATH` also verifies a portable artwork bundle against the composed
catalog and returns its checked `artworkHash`. Output is one JSON
object with `schemaVersion`, `baseHash` and `catalog: {snapshot, hash}`. Packages
are sorted by namespace, then appended in their authored variant order. Existing
base IDs remain stable. Duplicate namespaces and invalid compositions are
rejected atomically. With no packages the base snapshot and fingerprint remain
unchanged. `GlobEngine.composeBuildings` checks the binary's advertised capability
and base fingerprint and retains its exact canonical output.

Sprites have a local `key` and ordered `frames`. Each frame declares `imageHash`,
`width`, `height`, and optionally `teamColorHash` for the existing rotated-color
layer. A variant uses `package:<sprite-key>` in `gameSprite` or `miniSprite`, or
references installed `data/gfx/` artwork. Composition replaces package references
with content-addressed sprite paths. Damage frame ranges, miniature indices and
all sixteen connected-segment frames must fit the declared sprite. Artwork hashes
contribute to catalog identity. Composition validates declarations; it does not
read, install or render the declared image bytes.

The platform ZIP helpers export deterministic archives containing `manifest.json`
and `assets/<sha256>.png` or `.webp`. Imports accept stored or deflated ZIP32
members, check hashes and CRCs, and reject traversal, symlinks, duplicate members,
undeclared images, ambiguous JSON and decompression beyond the upload bound.
Image normalization accepts still PNG/WebP frames, verifies declared dimensions,
then retains lossless sRGB WebP with transparency and no source metadata.

Shared limits are 32 MiB for the archive and its expanded contents, 8 MiB for
manifests, 512 pixels per frame side, 256 image/layer entries per package, and
64 MiB of decoded custom pixels per composition. The existing 4,096-variant,
experiment and resolved-snapshot bounds also apply.

The standalone `G2BA0001` artwork bundle contains a canonical sprite manifest and
hash-addressed normalized WebP bytes. The native decoder verifies hashes, image
headers, decoded pixels, dimensions, catalog references and frame bounds before producing an
in-memory community asset mount. Community mounts cannot shadow installed
artwork or fall through to filesystem assets. Format 145 embeds the bundle after the resolved catalog in map, save and replay
headers. Older files load without a bundle. The native loader verifies the
embedded bytes before mounting sprites, including in headless validation.
Composing packages alone does not publish a release.

Related: [features and content](README.md).
