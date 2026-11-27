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
- adds a page style before them: `user-select: none` (with the `-webkit-`, `-moz-`
  and `-ms-` prefixes), `-webkit-touch-callout: none` and `touch-action: none`
  (with `-ms-touch-action`) on `body`, so a long press or double tap opens no
  magnifier, selection or callout and touch never pans the page (the CrazyGames
  technical requirements and GamePix submission guidelines). The game's DOM text
  fields (`input`, `textarea`) keep `user-select: text` and the default callout,
  so typing, selecting and copying in them still work;
- adds `maximum-scale=1,user-scalable=no` to the viewport meta (GamePix), keeping
  the shell's other viewport settings. Only portal uploads change; the website
  build keeps the shared shell as it is;
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
In every portal the game also offers no fullscreen control of its own (CrazyGames
forbids one; the portal's page has its own button): Settings > Display has no
Window mode and F11 does nothing (`ApplicationHost::fullscreenControlAllowed`).

A portal's first visit lands in gameplay, as CrazyGames ("land in gameplay") and
Poki (easy onboarding) ask: instead of the first-run questions and the main menu,
the game loads the introduction lesson at once (`PortalPolicy::landsInFirstLesson`,
`Application::mainMenu`) and marks the introduction offered, so later visits open
the main menu. The lesson reports `start`, `end` and `leave` like any match (no
ad before it, as it is the page's first match). Leaving it asks the first-run
questions it skipped (the touch audio and hand choices), then opens the main menu,
or the lesson list when the player chose it from the lesson's end.
`ads` is the profile's `ads` field: true only for the five portals marked in the
table above. Like the other fields it fails closed: only a JSON `true` turns it on.

## Runtime

`portal-core.js` keeps portal code out of the shared shell. It wraps
`AudioContext`, so every Web Audio context the game creates (sound effects and
music) can be silenced while a portal pauses or mutes the game; audio the game
reactivates meanwhile is suspended again, and a hidden page stays suspended for the
game's own visibility handler. It reads the shell's download bar as loading
progress and the body's `ready` class as the interactive main menu, and reports
gameplay as active while the player is in a match, play is not stopped, the page
is visible and neither the portal nor an ad holds the game; the menus are not
gameplay. Play stops while the in-game menu, or a dialog opened from it (save,
load, options, a confirmation) or Settings, covers the match, and while the game
is paused (`pause` and `resume` below, from `GameGUI::playStopped` through
`MatchPlayReporter`); the side panels (alliances, objectives, statistics) and chat
are play. Each change is reported once: opening the menu of a paused game is one
stop, and only a return to play is a start. Yandex Games also counts a window
without focus as a hidden page (requirements 1.3 and 1.19.3): `watchShell` with
`{focus: true}` reports gameplay stopped and silences audio on `blur` until
`focus`, and a page that loads without focus starts silent. The simulation keeps
running meanwhile, as only a hidden page pauses the game; CrazyGames' choice to
ignore tab switches (`{visibility: false}`) is unchanged and the other portals
ignore focus. Each adapter turns
these signals into its SDK's calls; without the SDK (an ad blocker, a local test)
an adapter does nothing and the game runs.

## Ad breaks

The game reports natural breaks to the page through
`ApplicationHost::matchBreak` (native builds ignore it), which reaches
`window.glob2PortalHost.matchBreak(point)` from `portal-core.js`:

| Point | When | Ad? |
| --- | --- | --- |
| `start` | a local match is about to run its first tick, after the player chose to play (from the menu, the next tutorial lesson or a loaded game) | yes, except before the first match since the page loaded (GameDistribution: its required pre-roll runs there; Poki: every start) |
| `end` | the match is over and its results show | no; gameplay stops |
| `leave` | the player leaves the finished match for the menu | yes, except on Poki |
| `pause` | play stops inside a local match: the in-game menu opens or the game is paused | no; gameplay stops |
| `resume` | play continues: the menu closes and the game is not paused | Poki only; gameplay starts after any break |

Each adapter passes `createBreaks` the points that may ask for an ad (`points`,
by default `start` and `leave`), whether the first match may (`firstStart`) and
our minimum gap (`minInterval`, by default 3 minutes; 0 turns it off). A point
that may not ask changes only gameplay: it never asks for an ad and neither starts
nor resets the gap. Leaving a match from the menu reports `end` and `leave` with
no `resume` in between, so gameplay stops once.

Poki asks for `commercialBreak()` before every `gameplayStart()` the player shows
intent to continue, as many times as possible, and its own system decides whether
an ad plays ([HTML5 SDK](https://developers.poki.com/guide/sdk-html5)). Its adapter
therefore asks at every `start`, the first match included, and at every `resume`
of a paused match, never at `leave` (returning to the menu is not a return to
play), with no gap of ours.

| Portal | Gameplay events around the menu and pauses |
| --- | --- |
| Poki | `gameplayStop()` / `gameplayStart()` |
| CrazyGames | `SDK.game.gameplayStop()` / `gameplayStart()` |
| Yandex Games | `features.GameplayAPI.stop()` / `start()` |
| GameDistribution | none (its SDK has no gameplay events; `SDK_GAME_PAUSE` still holds the game) |
| GamePix | none (its lifecycle has only `gameStart()`, sent once when the first match plays; its `pause` event still holds the game) |

Relay (online) matches report nothing, pauses included, so a break never delays a
lockstep game.
`createBreaks` asks the adapter's SDK for an interstitial only when the policy has
`ads: true`, one break at a time, and never during play or on first load. Except
on Poki, after every break, whether or not an ad played, it skips further breaks
for 3 minutes from when the break ended, on top of the SDK's own limits: leaving a
match and starting the next is one break, never two back to back.
GameDistribution's pre-roll starts that clock too; an SDK that is not there to ask
starts none. While a
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

| Portal | Call | Points | First match | Interval |
| --- | --- | --- | --- | --- |
| Poki | `PokiSDK.commercialBreak(onStart)`, between `gameplayStop()` and `gameplayStart()` | `start`, `resume` | asks | Poki's system only; no gap of ours |
| GameDistribution | `gdsdk.showAd()`, never before `SDK_READY`; `SDK_GAME_PAUSE`/`SDK_GAME_START` mark the ad | `start`, `leave` | pre-roll, which starts our 3 minutes | ours (3 minutes after every break) and the SDK's |
| Yandex Games | `ysdk.adv.showFullscreenAdv({callbacks: {onOpen, onClose, onError}})` | `start`, `leave` | no ad | ours and Yandex's (too-frequent calls close at once) |
| CrazyGames | `CrazyGames.SDK.ad.requestAd('midgame', {adStarted, adFinished, adError})`; audio mutes at `adStarted` | `start`, `leave` | no ad | ours and the SDK's 3-minute cooldown (`adCooldown`) |
| GamePix | `GamePix.ads.interstitial()` (or the legacy `interstitialAd()`); its `pause`/`resume` events hold and mute | `start`, `leave` | no ad | ours; GamePix documents none |

CrazyGames shows no ads during Basic Launch (`adError` with `adsDisabled`); the
call is the one Full Launch uses.

## Touch or mouse presentation

The shell chooses the touch interface when the device reports touch
(`navigator.maxTouchPoints > 0` or `(any-pointer: coarse)`, `publishPresentationMetrics`
in `shell.html`, read every frame through `Module.presentationMetrics`). A portal
adapter that knows better sets `window.glob2PresentationOverride = {touch}` and
dispatches a `glob2-presentation-override` event; the shell then publishes that
value instead. CrazyGames asks games not to rely on touch detection, which gives
touchscreen laptops mobile controls, and to use its SDK's system info instead, so
`crazygames.js` sets the override from `SDK.user.systemInfo.device.type` after
`init()`: `mobile` and `tablet` are touch, `desktop` is mouse, and any other value
leaves the shell's own detection. The player's Desktop, Tablet or Phone choice in
Settings still wins over both.

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
