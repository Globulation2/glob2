# Merge verification

All six rendering PRs (#877, #879, #881, #882, #885, #887) and wasm32 repair #888 were squash merged into master. Final master: `4c3f6074ff1fd1c5f91dc9a7e1fac448d274d98e`. Its source tree exactly matches rebased top `2409edc8444d693828cf36755eceba967bd9fd36`. The remaining commits were rebased onto each merged predecessor to resolve ancestry conflicts caused by squash merges.

Compared with validated source `1807fbf8e`, the only additional files are `browser/shell.html` and `docs/multiplayer/history-and-web.md`, from concurrent master loading-screen change #890 (`82007aa6d`). All engine/rendering/test source is byte-identical to the original validation. Serial and threaded Wasm SHA256 hashes also match the original binaries. [Machine-readable verification](verification.json).

Focused refreshed integration: browser packaging passed, then Chromium and Firefox threaded startup/settings/quit checks both passed (2 cases, 57 seconds). Commands:

```sh
CCACHE=1 taskset -c 0-11,16-27 scons target=web release=1 -j12 web-package
cd browser
GLOB2_TEST_RENDERER=software npx playwright test tests/runtime-build.spec.js --project=chromium --project=firefox --grep 'threaded startup' --reporter=line
```

[Build log](merge-browser-build.log), [test log](merge-browser-startup.log). Same environment as the [full validation](../final-snapshot-rendering/README.md). No full native or browser suite rerun after the shell-only change; prior coverage and platform/performance limits remain. Master CI proceeds asynchronously; no running master workflow was canceled.
