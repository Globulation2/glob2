# Web portal packaging

Web game portals host their own copy of the browser game inside an iframe on their
domain. Each [browser publication](browser.md) builds an upload for every portal
whose limits the full package meets: the `portals` matrix job in
`browser-release.yml` repackages the verified `browser-release` artifact, so the
runtime is byte-identical to the published browser game. Each matrix leg uploads a
`<portal>-upload` artifact (`glob2-VERSION-<portal>.zip`) and writes its size and
file count to the step summary. To rebuild the uploads for an existing tag without
republishing the browser game, dispatch `browser-release.yml` with `publish` off.

| Portal | Page | SDK | External links, online sign-in | In the release matrix |
| --- | --- | --- | --- | --- |
| GamePix | [gamepix.md](gamepix.md) | GamePix SDK v3 | no, no | yes |
| CrazyGames | [crazygames.md](crazygames.md) | CrazyGames SDK v3 | no, no | yes |
| Newgrounds | [newgrounds.md](newgrounds.md) | none | yes, yes | yes |
| Game Jolt | [gamejolt.md](gamejolt.md) | none | yes, yes | yes |
| GameDistribution | [gamedistribution.md](gamedistribution.md) | GameDistribution HTML5 SDK | no, no | yes |
| Yandex Games | [yandex.md](yandex.md) | Yandex Games SDK (`/sdk.js`) | no, no | no: over 100 MB unpacked |
| Poki | [poki.md](poki.md) | Poki SDK v2 | no, no | no: waits on the small download build |

## Package

`platforms/web/package-portal.py --portal <name>` checks the input with
`package-static.py --verify` and writes a reproducible zip with `index.html` at its
root. Portals require every game file to load by a relative path, so it:

- bundles the website-root assets `index.html` shares with the online app
  (`/theme.js`, `/favicon-32.png` and `/signin/assets/*`) from the
  `@glob2/design-system` revision that `services/online/package.json` pins, under
  `theme.js` and `brand/`, with their licences under `licenses/brand/`;
- fails on any other root-absolute or external reference in `index.html`;
- drops gzip sidecars and `SHA256SUMS`, which only the Google Cloud Storage
  publication uses;
- injects, before the first script, the portal policy and any SDK configuration in
  one inline script, then the portal's scripts in the order its profile lists:
  the SDK, `platforms/web/portal-core.js` and the portal's adapter;
- fails when the zip (or, for portals that measure it so, the unpacked files)
  exceeds the portal's size limit, when it has more files than the portal accepts,
  or when a file name breaks the portal's naming rule.

Each portal's profile in `package-portal.py` lists its SDK, scripts, policy, limits
and the developer-documentation URLs the limits come from. Run it locally with:

```
python3 platforms/web/package-portal.py build/browser-static --portal crazygames \
  --design-system ../glob2-design-system --output artifacts/portals/glob2-crazygames.zip --summary
python3 platforms/web/package-portal.py --portal crazygames --verify artifacts/portals/glob2-crazygames.zip
```

`platforms/web/package-gamepix.py` remains as `--portal gamepix`.

## Portal policy

The inline script sets `window.GLOB2_PORTAL = {name, externalLinks, onlineSignIn}`
before any game script runs. The game reads it to hide links that leave the portal
and online sign-in where a portal bans them; outside a portal upload the object is
absent and nothing changes.

## Runtime

`portal-core.js` keeps portal code out of the shared shell. It wraps
`AudioContext`, so every Web Audio context the game creates (sound effects and
music) can be silenced while a portal pauses or mutes the game; audio the game
reactivates meanwhile is suspended again, and a hidden page stays suspended for the
game's own visibility handler. It reads the shell's download bar as loading
progress and the body's `ready` class as the interactive main menu, and reports
gameplay as active while the menu is ready, the page is visible and the portal has
not paused the game. Each adapter turns these signals into its SDK's calls; without
the SDK (an ad blocker, a local test) an adapter does nothing and the game runs.

Portal pages do not send the cross-origin isolation headers, so players get the
serial runtime ([hosting](../../platforms/web/README.md)). Saves stay in IndexedDB
on the player's device.

## Measured size

The 0.11.8 package (`browser-release` artifact of the 2026-11-24 publication) as
each portal's upload:

| | Zip | Unpacked | Files |
| --- | --- | --- | --- |
| Upload | 132.0 MB | 219.2 MB | 494 or 495 |

Before the main menu is interactive the serial runtime downloads 54.5 MB in an
English interface (the runtime's 29.9 MB WebAssembly, the 10.4 MB `core` package,
the `game` packages and scripts), 66.5 MB in another language (adding the
`translations` packages) and 74.6 MB in Chinese, Japanese or Korean (adding
`font-cjk`). With gzip transfer that is 25.4, 28.8 and 31.9 MB. Measured by loading
an unpacked upload without isolation headers in headless Chromium and summing the
files requested before the body gained `ready`.

[Release index](README.md) · [Documentation index](../README.md).
