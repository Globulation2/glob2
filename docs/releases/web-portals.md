# Web portal packaging

Web game portals host their own copy of the browser game inside an iframe on their
domain. Each [browser publication](browser.md) builds an upload for every portal:
the `portals` matrix job in `browser-release.yml` repackages a verified browser
package, so the runtime is byte-identical to the published browser game. Each leg
uses one edition ([editions](../browser/editions.md)): the standard
`browser-release` artifact, uploaded as `<portal>-upload`
(`glob2-VERSION-<portal>.zip`), or the download-size lite edition
`browser-release-lite`, uploaded as `<portal>-lite-upload`
(`glob2-VERSION-<portal>-lite.zip`). Each leg writes its size and file count to the
step summary. To rebuild the uploads for an existing tag without
republishing the browser game, dispatch `browser-release.yml` with `publish` off.

| Portal | Page | SDK | External links, online sign-in | Ads | Release upload |
| --- | --- | --- | --- | --- | --- |
| GamePix | [gamepix.md](gamepix.md) | GamePix SDK v3 | no, no | yes | standard |
| CrazyGames | [crazygames.md](crazygames.md) | CrazyGames SDK v3 | no, no | yes (none in Basic Launch) | lite: 50 MB initial download |
| Newgrounds | [newgrounds.md](newgrounds.md) | none | yes, yes | no | standard |
| Game Jolt | [gamejolt.md](gamejolt.md) | none | yes, yes | no | standard |
| GameDistribution | [gamedistribution.md](gamedistribution.md) | GameDistribution HTML5 SDK | no, no | yes | standard |
| Yandex Games | [yandex.md](yandex.md) | Yandex Games SDK (`/sdk.js`) | no, no | yes | lite: 100 MB unpacked |
| Poki | [poki.md](poki.md) | Poki SDK v2 | no, no | yes | lite: small download |

Ads appear only in the packages of portals that require or expect them (owner
decision, 2026-11-26): never on our own site, itch, the stores, Game Jolt or
Newgrounds. Nothing is sold and there are no in-app purchases, so there are no
rewarded ads either.

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

The inline script sets `window.GLOB2_PORTAL = {name, externalLinks, onlineSignIn, ads}`
before any game script runs. The game reads it to hide links that leave the portal
and online sign-in where a portal bans them, and in every portal it drops Quit from
the main menu; outside a portal upload the object is absent and nothing changes.
`ads` is the profile's `ads` field: true only for the five portals marked in the
table above. Like the other fields it fails closed: only a JSON `true` turns it on.

## Runtime

`portal-core.js` keeps portal code out of the shared shell. It wraps
`AudioContext`, so every Web Audio context the game creates (sound effects and
music) can be silenced while a portal pauses or mutes the game; audio the game
reactivates meanwhile is suspended again, and a hidden page stays suspended for the
game's own visibility handler. It reads the shell's download bar as loading
progress and the body's `ready` class as the interactive main menu, and reports
gameplay as active while the player is in a match, the page is visible and neither
the portal nor an ad holds the game; the menus are not gameplay. Each adapter turns
these signals into its SDK's calls; without the SDK (an ad blocker, a local test)
an adapter does nothing and the game runs.

## Ad breaks

The game reports natural breaks to the page through
`ApplicationHost::matchBreak` (native builds ignore it), which reaches
`window.glob2PortalHost.matchBreak(point)` from `portal-core.js`:

| Point | When | Ad? |
| --- | --- | --- |
| `start` | a local match is about to run its first tick, after the player chose to play (from the menu, the next tutorial lesson or a loaded game) | yes, except before the first match since the page loaded (GameDistribution: its required pre-roll runs there) |
| `end` | the match is over and its results show | no; gameplay stops |
| `leave` | the player leaves the finished match for the menu | yes |

Relay (online) matches report nothing, so a break never delays a lockstep game.
`createBreaks` asks the adapter's SDK for an interstitial only when the policy has
`ads: true`, one break at a time, and never during play or on first load. While a
break runs it holds the game: the session reports gameplay stopped, audio is
silenced (through the same `AudioContext` wrapper as portal pauses), and
`glob2PortalHost.held` is true. The browser host samples `held` once per frame;
while it is true `Application` runs no screen frames, like a hidden page, so the
simulation and input stop, and a match about to start waits. The SDK's own pause
events (GameDistribution `SDK_GAME_PAUSE`, Yandex `game_api_pause`) hold the game
the same way. The break ends when the SDK's promise or callback settles; an SDK
that is missing, rejects, throws, or reports no fill, a cooldown or an ad blocker
simply runs no ad. A break whose ad has not started within 10 seconds, or has run
for 3 minutes, releases the game anyway.

| Portal | Call | First match | Interval |
| --- | --- | --- | --- |
| Poki | `PokiSDK.commercialBreak(onStart)`, between `gameplayStop()` and `gameplayStart()` | no ad | Poki's ad timer |
| GameDistribution | `gdsdk.showAd()`; `SDK_GAME_PAUSE`/`SDK_GAME_START` mark the ad | pre-roll | the SDK's |
| Yandex Games | `ysdk.adv.showFullscreenAdv({callbacks: {onOpen, onClose, onError}})` | no ad | Yandex's (too-frequent calls close at once) |
| CrazyGames | `CrazyGames.SDK.ad.requestAd('midgame', {adStarted, adFinished, adError})`; audio mutes at `adStarted` | no ad | the SDK's 3-minute cooldown (`adCooldown`) |
| GamePix | `GamePix.ads.interstitial()` (or the legacy `interstitialAd()`); its `pause`/`resume` events mute | no ad | at least 3 minutes between our requests |

CrazyGames shows no ads during Basic Launch (`adError` with `adsDisabled`); the
call is the one Full Launch uses.

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
