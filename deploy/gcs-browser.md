# Browser-only Google Cloud Storage deployment

The September 9, 2026 hosting smoke deployment uses project `pharaoh-418820`
and public bucket `glob2-browser-pharaoh-418820-20260909` in `us-central1`.

Play at https://glob2-browser-pharaoh-418820-20260909.storage.googleapis.com/index.html.
Use the explicit `/index.html` path on this HTTPS object endpoint.
This deployment runs single-player locally in the browser; no YOG lobby,
router, or WebSocket gateway is deployed. Multiplayer menu entries still exist,
but multiplayer is unavailable on this host.

Build and upload only the four runtime artifacts (never the build directory,
which also contains compiler caches and intermediate files):

```sh
scons target=web release=1 -j8
python3 browser/package-static.py
gcloud storage cp \
  'build/browser-static/index-*.js' \
  'build/browser-static/index-*.wasm' \
  'build/browser-static/index-*.data' \
  gs://glob2-browser-pharaoh-418820-20260909/ \
  --cache-control='public,max-age=31536000,immutable'
gcloud storage cp build/browser-static/index.html \
  gs://glob2-browser-pharaoh-418820-20260909/ \
  --cache-control='no-cache'
```

The bucket uses uniform bucket-level access, `allUsers` with
`roles/storage.objectViewer`, and website main page `index.html`.
Cloud Storage serves the HTML as `text/html` and WebAssembly as
`application/wasm`. Hosting setup follows the
[Google Cloud static website documentation](https://docs.cloud.google.com/storage/docs/hosting-static-website).
The packager gives each build's assets versioned names so cached WebAssembly
cannot be mixed with a newer build. Upload assets before replacing `index.html`,
which browsers revalidate on reload. Local saves belong to the browser profile
and hosting origin.

Run the existing single-player smoke checks against the actual hosted files:

All four checks passed in Chromium on September 9, 2026: runtime validation,
startup/storage restore, tutorial restart, and custom-game pause/save/reload/
resume with audio activation. The deployed game build is from commit
`1658ff670881a504a4269bcf5ece7ceb0b2e906f`, plus the September 9 sprite
pixel-format correction in `DrawableSurface::convertForUpload`. The color
regression checks passed for software and WebGL2 in Chromium, Firefox, and
WebKit before publishing the correction.
Both color checks also passed in Firefox against the hosted versioned assets
(`0587a80f464c65cd`). If an old cached page persists, open
`/index.html?v=0587a80f464c65cd` to fetch the current entry page.

```sh
cd browser
GLOB2_TEST_URL=https://glob2-browser-pharaoh-418820-20260909.storage.googleapis.com \
GLOB2_TEST_ENTRY_PATH=/index.html \
npx playwright test tests/single-player.spec.js tests/team-colors.spec.js \
  --project=chromium \
  --grep 'starts with|tutorial sessions|custom match pauses|preserves the red'
```
