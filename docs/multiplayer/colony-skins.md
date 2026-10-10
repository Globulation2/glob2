# Colony skins and purchases

Ownership, client loading and purchase processing for colony appearances.

## Colony skin ownership

Skin identity (`colony_skins`) is separate from immutable published paint
(`colony_skin_versions`). Each version references two blobs, a colour atlas
(`texture_sha256`) and a material map (`material_sha256`), plus the layout
`colony-v2`, building color and manifest digest. Database triggers reject edits
and deletion of published versions; moderation disables the parent skin. Blob
garbage collection retains both images of every published version and all referenced sprite derivatives. Account deletion removes private drafts
and equipment, replaces owned skin names with “Deleted skin” and disables their
paint while preserving immutable version identifiers for match history. Guest
retention keeps accounts referenced by skin reports so moderation records remain
valid. Publishing, draft saves and reporting use shared database rate limits
across API replicas.

`colony_skin_equipment` records a selected version. The equipment service checks
active registered accounts, unexpired and unrevoked entitlements, and ownership
of custom designs. Match creation must revalidate selection. `match_colony_skins`
reserves frozen per-team appearances and public signed assertions, separate from
private join tickets. The first assignment delivery freezes all team choices in
one transaction, including default appearances. Shared colonies use the lowest
numbered human seat's selection. Reconnects refresh assertion lifetime without
changing content. The distinct `glob2-colony-skin+jwt` type and
`glob2-colony-renderer` audience bind account, match, team and version; they
cannot be used as relay join tickets. `GET /api/v1/matches/:id/skins` exposes
already frozen appearances for spectators/history without freezing new choices.
Native clients verify these assertions before downloading paint. Checkout requires
server-side Stripe configuration; without it, purchases remain unavailable.

Authenticated `GET /api/v1/skins` lists presets and the caller's versions;
`PUT /api/v1/skins/equipped` accepts a version ID or null to restore default art,
plus an optional RGB building color. The chosen color is independent of the
immutable preset paint and is frozen/signed alongside the version for a match.
`POST /api/v1/skins/publish` requires the designer entitlement. It accepts a name,
optional owned skin ID for another version, RGB building color, optional swarm
mesh and integer `swarmViewAngle` (0–359, default 0), and two base64 PNG or WebP images in layout `colony-v2`. Both are still
512×512 pixels made of four 256×256 quadrants: worker top-left, warrior
top-right, explorer bottom-left, swarm bottom-right.

- `imageBase64`, the colour atlas, is at most 1 MiB. The server re-encodes it as
  an opaque sRGB lossless WebP without metadata.
- `materialBase64`, the material map, is at most 256 KiB. Every pixel is grey
  (R = G = B), opaque, and a material id below the count registered in
  `libgag/shaders/skin-materials.json` (mirrored as `COLONY_SKIN_MATERIALS` in
  the protocol package). Anything else is a 400. The server re-encodes it as an
  8-bit lossless WebP. Native clients built with the registry shade ids beyond
  their own catalogue as matte, so later materials degrade gracefully; clients
  from before the registry reject such a skin and keep that team's previous
  appearance, since they only knew ids 0 to 3.

The version's `manifestSha256` is described below; native clients recompute it.
Publishing identical content again returns the existing version. Publication and
equipment are immediate. The publication transaction queues `skin:render:<revision>`
with three bounded attempts, deduplicated by immutable version and render revision.
`skin-render-worker` registers its revision and backfills published designs and
presets at startup. Its one native OpenGL process runs with Mesa/llvmpipe under
Xvfb, a five-minute timeout and resource limits. `colony_skin_sprites` records
`pending`, `ready` or `failed`; manifests and page references commit together only
after every image is validated and stored. The library exposes `softwareStatus`
and explains preparing or unavailable software artwork while keeping equipment
available. Ready derivatives are immutable. Renderer upgrades create new records
without rewriting published paint.

A signed optional `softwareSprites` descriptor carries `format`,
`manifestSha256` and `renderRevision`. Its optional `source` identifies the immutable
paint, material and canonical source manifest separately from WebP wire renditions.
Clients verify this signed identity before accepting a bundle; older descriptors
use the signed version identity. Appearance refresh pins the first ready
bundle to the match; later renderer revisions cannot replace it. Its addresses
come from the trusted instance origin:
`GET /api/v1/skins/versions/:id/sprites/:manifestHash/manifest` and
`.../pages/:pageHash`. These endpoints serve only ready, linked blobs and retain
the source version's moderation checks. Blob garbage collection includes both
bundle manifests and pages.
`GET /api/v1/skins/versions/:id/texture` serves the colour atlas and
`GET /api/v1/skins/versions/:id/material` the material map, both as `image/webp`
with the blob SHA-256 as ETag; disabled skins return 404. Older published sources remain immutable. The API caches lossless WebP wire
renditions in `image_webp_renditions`, signs their exact texture/material hashes
and recomputes the wire manifest hash while retaining version IDs. Apply migration
0041 and 0042 before deploying the API and worker together with the WebP-only client.
Old clients that require PNG skins need upgrading; existing signed PNG tickets
must be refreshed before a new client can install their appearances. Skin image
requests include `?sha256=<wire hash>` so cached PNG responses from earlier
releases cannot satisfy requests for the new renditions. Map catalog
preview URLs end in `preview.webp`; existing engine-produced PNG preview sources
are converted through the same persistent rendition cache. End-user PNG/WebP
uploads remain accepted as imports on the server.

Raw uploads and arbitrary blob keys are never served
by these endpoints. The designer opens saved working designs or copies an owned
preset into a new design. Applying an edit creates an immutable content snapshot
and selects it; frozen match appearances retain their paint.

The `/skins` route opens the skin collection for registered accounts and a trial
painting workspace for guests, beside the
shared persistent sidebar. All app pages retain this sidebar, with a compact icon
rail below 1100 pixels and a drawer for expanded navigation.
Brush and eraser paint every surface underneath the cursor, including hidden
surfaces. The eyedropper samples visible geometry. Horizontal right-drag,
Alt-drag or the Rotate tool turns the model; touch uses explicit Paint/Rotate tools and
two-finger pinch zoom. The view menu and +/− keys also adjust inspection zoom.
Animation starts paused and painting freezes its displayed
pose. Each stroke and accepted pattern is one undo transaction. The toolbox,
material swatches (one sphere per registered material, grouped as in the
registry, shaded by the game's own material GLSL including fur shells), model
and pose strips float over the viewport. Patterns, paint copying and shape
selection use focused dialogs; the collection is a separate screen and the Shop
opens from it. The workspace and dialogs use the web application’s shared Meadow
and Night colony themes, following the device setting or saved preference. The
sidebar's theme control preserves paint and editing state.
Paint, building and pattern colors use an editor-owned palette that expands in
place, with a saturation/brightness area, hue slider, preset swatches and hex
entry. Colors update immediately without opening an operating-system dialog.
Arrow keys adjust saturation horizontally and brightness vertically in the color
area; Shift increases the step. The existing model eyedropper also updates the
paint palette.

Glob meshes share paint coordinates across matching front/back and top/bottom
surfaces, including limb pairs exchanged by their flipping gait. Brush coverage
includes every projected contributor to a texel, applying its strongest coverage
once. Changing a shared texel changes all matching surfaces.
Eyedropper and projected patterns use the closest visible contributor deterministically.
Pattern previews always render the baked atlas, including this repetition.
Camera-projected stripes, spots, checker, chevrons, waves and speckles use the
paused pose and chosen inspection angle. Curated solid, mirrored bands/spots and
mottled fills use per-mesh rest-space compatibility charts, with limited sizes and
densities. Both keep the existing atlas layouts. No UV painting UI or layers are
exposed. Copying raw paint between models is a separate action with a result preview.

The inspection camera stays at a fixed height and angle while the model rotates
around its upright axis; vertical drags do not tilt it. The swarm's
separate **Choose final view** mode changes only azimuth around its standardized
camera ring; accepting it restores the inspection camera. Unit game rendering
continues to select animation directions normally. Building color is separate
from painted color and also colors the rendered material swatches. Without
WebGL2, saved skins and the shop remain accessible while the viewport offers a retry.

Each version also names the swarm mesh its paint is laid out for (`swarmMesh`):
`classic`, the original swarm and the default, or one of the generated shapes
`crown`, `clutch`, `toadstool`, `coral`, `skep` and `bloom`. The protocol's
`SWARM_MESHES` and the game's `src/online/SwarmMeshCatalog.h` list the same ids in
the same order. Because paint is laid out per mesh, the mesh belongs to the
immutable version, and the same paint on two meshes is two versions. The manifest
digest is SHA-256 over the compact JSON object `skinId`, `textureSha256`,
`materialSha256`, `layout`, `buildingColor`, in exactly that key order, followed
by `swarmMesh` only when it is not `classic`, then `swarmViewAngle` only when it is
nonzero. Zero therefore retains all existing manifest hashes. Older clients reject
nonzero angles through their manifest check and use classic cosmetic fallback;
current game clients recompute the complete manifest before
showing a skin. The swarm's paint and materials always come from the swarm
quadrant, whichever mesh is chosen. Clients without mesh choice reject skins for
other meshes and show classic art for that team, rather than painting them onto
the classic swarm. Likewise, an API that finds a stored mesh id it does not know
(after a rollback) omits that version from skin lists and match appearances
instead of signing it, and restores such a draft on the classic swarm.

Registered active accounts keep private working designs in `colony_skin_designs`,
one mutable canvas per owned custom skin, with up to 100 active designs per account. `GET /api/v1/skins/collection` returns
one entry per design and owned presets, together with the selected appearance
and designer eligibility. Existing account drafts are migrated without changing
match equipment; other existing designs initialize from their newest immutable
snapshot when first opened through the collection. The legacy single-draft and
publication APIs remain available for compatibility.

`POST /api/v1/skins/designs` creates a design using a client-generated UUID
(idempotent retries), optionally copying an owned design or preset.
`PUT /api/v1/skins/designs/:id` replaces the working canvas only when the supplied
revision matches. Saving needs no designer entitlement and never publishes a
snapshot. `POST /api/v1/skins/designs/:id/use` checks that revision and the designer
entitlement, creates or reuses an immutable snapshot, and selects it in one
transaction. A failed apply leaves the previous selection intact. Edits to an
active design remain unapplied until **Use in game** is clicked. Deleting a design
archives its identity, removes its private working canvas, and clears equipment
if selected; historical versions and match appearances remain available. Archive
state is independent of moderation, which can still disable the paint.

The Skins destination is a collection page, with six designs or presets per page
to bound simultaneous WebGL previews. Each design has an editor with automatic
account saving, a truthful save status, and **Use in game**; versions and manual
save/restore destinations are not exposed. Saving is serialized, debounced after
edits, and retried after connection recovery. Account-scoped browser recovery
keeps pending changes per design across navigation and checkout. Cross-device
conflicts preserve local work and offer **Load account changes** or **Keep mine as
a new skin** rather than silently overwrite. Guests can paint with browser recovery
and sign in to carry their work into a saved design. Theme remains a site control;
building color and copying paint live in the toolbox, and toolbox layout reset
lives in its options menu. Checkout saves the working design or browser recovery
before navigation and returns to the Shop dialog.

Match pages show their frozen colony looks and let signed-in players submit a
reason to `POST /api/v1/skins/versions/:id/reports`. Each account reports a version
at most once. Moderators review the paginated open/closed queue at
`GET /api/v1/admin/skin-reports`, then resolve a report with
`POST /api/v1/admin/skin-reports/:id/resolve` (`dismissed` or `disabled`, with a
reason). Resolution, optional skin disable, and audit records commit together;
concurrent retries do not duplicate the resolution audit. Moderators can restore
or disable a skin using `POST /api/v1/admin/skins/:id/moderation`.

Disabling affects the entire design, including every published version. Public
texture requests return 404, equip/publish checks refuse it, and refreshed match
assertions omit it. Frozen snapshot rows and immutable images remain intact, so
restoration uses the original content. The private moderator endpoints
`GET /api/v1/admin/skins/versions/:id/texture` and `.../material` permit review
of disabled paint with `private, no-store` caching. Existing clients still require moderation refresh
and a local hide control before this provides complete in-match moderation.


## Colony skin payments

Stripe-hosted checkout uses three server-defined products: `designer`, `stripes`,
and `spots`. The API seeds two immutable presets (colour atlas and material map
from `platform/apps/api/assets/skins/`) with stable IDs;
existing versions and moderation decisions are never overwritten at startup.
The web designer includes the store, account purchase history, and explicit
payment reconciliation after returning from Checkout. Configure `STRIPE_SECRET_KEY`, `STRIPE_WEBHOOK_SECRET`, and the
corresponding `STRIPE_PRICE_DESIGNER`, `STRIPE_PRICE_STRIPES`, `STRIPE_PRICE_SPOTS`.
Prices must be active fixed one-time Stripe prices. Test keys are the default;
live keys require explicit `STRIPE_LIVE_ENABLED=true`. Missing configuration
leaves painting available and purchases unavailable.

`POST /api/v1/skins/checkout` accepts a product and UUID request ID. Registered
active accounts are required. The server records the purchase before creating
Checkout, uses its ID as Stripe's idempotency key, and reuses pending purchases.
`GET /api/v1/skins/products` returns configured price/currency and availability;
`GET /api/v1/skins/purchases` lists the caller's recent purchases. A redirect
never grants access. `POST /api/v1/skins/purchases/reconcile` takes an owned
purchase ID and reads current Stripe state before changing its entitlement.

Configure the signed `/api/v1/skins/stripe-webhook` endpoint for
`checkout.session.completed`, `checkout.session.async_payment_succeeded`,
`checkout.session.async_payment_failed`, `checkout.session.expired`,
`charge.refunded`, `charge.dispute.created`, `charge.dispute.updated`, and
`charge.dispute.closed`. The scoped parser retains exact request bytes for SDK
signature validation. Event IDs are recorded after successful processing, and
purchase row locks serialize reconciliation. Every reconciliation re-reads the
session and verifies mode, owner, product price and quantity. It grants one
entitlement per purchase only after payment. Any refund revokes that grant;
unresolved/lost disputes suspend it and won/closed-warning disputes restore it
when the payment remains paid. Other grants for the same product are unaffected.

The implementation follows Stripe's [fulfillment guidance](https://docs.stripe.com/checkout/fulfillment)
and [webhook signature requirements](https://docs.stripe.com/webhooks).
When configured, each API process runs a reconciliation sweep every minute.
A sweep claims up to five due purchases with `FOR UPDATE SKIP LOCKED` and a
10-minute database lease before contacting Stripe. Successful pending/disputed
checks repeat after 15 minutes; settled payments repeat daily. Provider failures
keep the lease delay, and process crashes become retryable when it expires.
Shutdown stops taking further work and waits for the current check. This recovers
missed webhooks without overlapping checks across API replicas. Webhooks and the
account's check-payment action still reconcile immediately.

A repeated checkout reuses the recorded Stripe session instead of relying on
Stripe's [finite idempotency-key retention](https://docs.stripe.com/api/idempotent_requests).
Each purchase fixes the session expiry at 23 hours after its creation; fresh
creation requests stop after 22 hours. Retries keep the same expiry and key, so
an old creation request cannot produce another payable session after key expiry.
Creation and recording its ID are serialized under the purchase row lock.

If a crash loses the checkout ID, recovery starts after 24 hours, when its
creation window is closed. It reads [Checkout session pages](https://docs.stripe.com/api/checkout/sessions/list)
within that fixed window, validates purchase/account metadata, and persists the
pagination cursor. Each sweep reads at most one page per purchase. A recovered
session then undergoes the normal payment and price checks. Only a complete
scan with no match marks the purchase failed, allowing a new request; a scan
error leaves the purchase pending for retry. Expired recorded sessions also
become failed purchases. Production provider checkout verification is still
required before enabling a store.

### Game client appearance loading

Match assignments carry signed `glob2-colony-skin+jwt` assertions, bound to the
instance, match, team, account, immutable version (colour atlas and material
map hashes and manifest) and chosen building color. `SkinAuthorization` verifies Ed25519 with OpenSSL natively and asynchronous
WebCrypto in the browser. Assertions have a maximum 24-hour lifetime, with
30 seconds of clock tolerance. The client derives download URLs from its trusted
instance origin and verified version ID; an assignment cannot supply a texture
or key-server URL.

`SkinDownloads` fetches JWKS with a 64 KiB limit, then each version's colour
atlas (1 MiB limit) and material map (256 KiB limit), four at a time. It verifies
the signed SHA-256 values and 512×512 still WebP dimensions before image decoding. The loader stays attached to the view and refreshes the
trusted match appearance endpoint every minute, with a 512 KiB response limit.
A complete valid snapshot removes omitted teams immediately; additions require
fresh signature and texture verification. Failed or malformed refreshes retain
currently authorized paint until its signed expiry, then restore classic art.
The endpoint is not cacheable. Cached bytes are rechecked before reuse, and the cache
retains at most 64 managed textures. Failure leaves the classic appearance in
place. Downloads are polled from the view and never gate simulation startup.
Verified textures and building colors live in `MapRenderState`, not team state
or saved simulation data. The saved device preference **Show colony skins** is
available in Settings > Display and the in-game Options dialog. Turning it off
immediately restores classic units, swarms and building colors locally; verified
appearance refreshes continue, so turning it back on uses current authorization.
Software rendering uses the published sprite bundle for workers, warriors,
explorers and the selected swarm mesh and angle. Other buildings and zoomed-out
unit markers keep the signed building color. Without ready artwork, classic
sprites remain visible with that color. Software clients download neither paint
images nor meshes and create no OpenGL context for these skins. Unknown bundle
formats retain this fallback; optional metadata keeps old claims compatible.

The view verifies the signed descriptor, manifest hash and source identity before
requesting pages, then verifies each page's SHA-256, byte count and static WebP
header before decoding. Manifests are limited to 64 KiB, pages to 2 MiB compressed
and their fixed 1024×1024 (unit) or 128×128 (swarm) dimensions. Up to four fetches
run concurrently and at most one page decodes per view poll. Content-addressed
pages are shared between teams, with a 64 MiB decoded LRU cache and 256 MiB disk
cache whose bytes are revalidated on reuse. Completed offscreen requests release
their slots without decoding; camera movement cannot block subsequent downloads.
If disk writes fail, verified compressed buffers share the four-slot budget until
decoding, so artwork remains available without an unbounded memory queue.
Unused decoded pages are evicted;
expiry or moderation removes installed appearance. Existing animation mapping,
shadows, fog, zoom, clipping and the Show colony skins preference apply to both
rendering paths. This is presentation state and does not alter saves, simulation
checksums or `SIM_REVISION`.

Units animate from fitted rigs rather than the baked per-frame meshes: GSB1
blend-shape clips for workers and warriors and a GSR1 bone rig for the
explorer, all fitted to the baked clips so they keep the original metaball
look and paint layout. The baked GSK1 clips remain the fallback when a fitted
asset is missing or invalid, and `GLOB2_SKIN_RIGS=0` selects them in gameplay
for comparison. Sprite publishing always renders the rigs; its recipe digest
covers their bytes and decoders, so a changed rig never overwrites a published
bundle. Software clients continue using their authorized published sprites.
See [unit rigs](../../tools/unit-animation/README.md#unit-rigs-gsb1-blend-shapes-and-gsr1-bone-rigs).

Skin meshes are installed under `data/skins/colony-v1`; they share the web
designer's UV layout, each model sampling its own `colony-v2` quadrant. The browser ships them in an on-demand `skins` package
requested when visible paint is available. Classic rendering continues during
the download; hidden or unskinned colonies do not initiate it. Failed package requests retry at most every ten
seconds without stopping the match.

Each verified skin also selects its swarm mesh: `swarm.gsk`, derived from the
original art, for `classic`, or `swarm-<id>.gsk` for a shape generated by
`tools/skins/generate_swarms.py` (see the
[unit animation tooling](../../tools/unit-animation/README.md)). A skin naming a
mesh this client does not know is rejected like any other invalid assertion, and
a mesh file that fails to load leaves that colony's swarm on the classic sprite.
A nonzero final angle reconstructs model-space positions, rotates around the
world vertical axis, then applies the same game projection. Normals rotate with
the model. Each transformed mesh receives a fresh render-cache identity. Height,
target, radius, scale and ground alignment stay fixed around the ring. These are
appearance-only changes; simulation state and version gates are unchanged.

`tools/skins/export_views.py` (pinned Blender 3.6.23) emits versioned `.view.json`
sidecars bound to each GSK SHA-256 and the corresponding native constants in
`src/online/SkinViewTransforms.h`. It recovers the original unit/source camera and
separate depth scaling without changing GSK payloads, UVs, topology or poses.
Regenerate sidecars/constants whenever source mesh exports change. The web
viewport, brush and pattern engine share these transforms; the native rendering
path uses the generated swarm constants. `studio_thumbnails.py` regenerates the
model and action thumbnails from the shipped meshes.

Online replay recordings and native profile downloads have an optional
`<recording>.appearance.json` companion containing format version 1, instance
origin, match ID and SHA-256 of the recording. Replay bytes and version gates are
unchanged. Keep this companion with a copied or renamed replay (renaming both).
The client accepts at most 1 KiB of metadata, hashes recordings up to 64 MiB in
small chunks, and uses it only for an already trusted instance and matching
recording. Missing, stale, malformed or untrusted metadata leaves classic art.
Playback fetches fresh signed match appearance immediately and retains the usual
moderation refresh and local hiding. Companion write failures do not fail a
recording. Browser watch links create the same temporary companion only for the
hosting instance's exact match-replay route, without cross-origin redirects.

The live mesh renderer supports desktop OpenGL and WebGL2. A visible-scene
prepass rasterizes missing mesh/pose/paint combinations into persistent atlases
before map drawing. A least-recently-used cache holds at most 1,024 tiles across
four 2,048-square RGBA pages and a shared depth attachment (80 MiB maximum).
Paint revisions, mesh reloads and fresh texture lifetimes get distinct keys;
context teardown discards the cache. The prepass protects visible hits before
evicting old tiles, and overflow draws regenerate evicted poses on demand.
Identical units, wrapped copies and subsequent frames reuse those tiles, while their
composites retain the original ground-unit/building/air-unit order and visibility
rules. Unit meshes retain the original action/direction shadow layer beneath
the live geometry, using the same logical canvas (including HD shadow art when
available). The layer is emitted only after a mesh tile is ready, so fallback
cannot draw it twice. Requests are sorted by mesh and pose to share geometry uploads between
team textures. WebGL2 uses explicit GLSL ES shaders and an interleaved vertex
buffer; both backends restore the map renderer's state after the prepass. Context restoration recreates these resources
from retained meshes and paint. Native mobile rendering, live spectator
attachment and full performance validation remain required
before release.

[Multiplayer index](README.md) · [Documentation index](../README.md).
