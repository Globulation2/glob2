# GamePix publication

GamePix hosts its own copy of the browser game inside an iframe on its domain.
Each [browser publication](browser.md) also builds the GamePix upload: the
`gamepix` job in `browser-release.yml` repackages the verified `browser-release`
artifact, so the runtime is byte-identical to the published browser game. Download
the job's `gamepix-upload` artifact (`glob2-VERSION-gamepix.zip`) and submit it on
the GamePix developer dashboard. GamePix publishes no upload API, so this step is
manual until it does. To rebuild the upload for an existing tag without
republishing the browser game, dispatch `browser-release.yml` with `publish` off.

## Package

`platforms/web/package-gamepix.py` checks the input with `package-static.py --verify`
and writes a reproducible zip with `index.html` at its root. GamePix requires every
game file to load by a relative path, so it:

- bundles the website-root assets `index.html` shares with the online app
  (`/theme.js`, `/favicon-32.png` and `/signin/assets/*`) from the
  `@glob2/design-system` revision that `services/online/package.json` pins, under
  `theme.js` and `brand/`, with their licences under `licenses/brand/`;
- fails on any other root-absolute or external reference in `index.html`;
- drops gzip sidecars and `SHA256SUMS`, which only the Google Cloud Storage
  publication uses;
- injects the manual-lifecycle configuration, the GamePix SDK
  (`https://integration.gamepix.com/sdk/v3/gamepix.sdk.js`) and
  `platforms/web/gamepix.js` before the first script.

Run it locally against a release package with:

```
python3 platforms/web/package-gamepix.py build/browser-static \
  --design-system ../glob2-design-system --output artifacts/gamepix/glob2-gamepix.zip
python3 platforms/web/package-gamepix.py --verify artifacts/gamepix/glob2-gamepix.zip
```

## Runtime adapter

`gamepix.js` keeps portal code out of the shared shell. It reports the shell's
download bar to `GamePix.lifecycle.loading`, and calls `loaded()` and `gameStart()`
once the shell marks the body `ready`. The SDK's `pause` and `resume` events suspend
and resume every Web Audio context the game creates (sound effects and music). Audio
the game reactivates while GamePix has it paused is suspended again, and a hidden page
stays suspended for the game's own visibility handler. Without the SDK the adapter
does nothing.

GamePix pages do not send the cross-origin isolation headers, so players get the
serial runtime ([hosting](../../platforms/web/README.md)). Saves stay in IndexedDB on
the player's device; GamePix synchronizes only `localStorage`.

## Open items

These wait on GamePix's answers to our questions:

- external links (Discord, privacy policy, source code, online sign-in), which the
  submission guidelines ban;
- online play and accounts on our own servers;
- upload size and file-count limits (the package is about 130 MB and 490 files);
- an upload API, to replace the manual dashboard step;
- `GamePix.ads.interstitial()` between matches, which needs a bridge from the end of
  a game.

[Release index](README.md) · [Documentation index](../README.md).
