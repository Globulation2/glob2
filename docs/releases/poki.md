# Poki publication

Poki gets the download-size lite edition, not the full package. Poki states no
size limit but asks for a small file size and progressive loading: players leave
after about ten seconds of loading
([requirements](https://developers.poki.com/guide/requirements-quality)). The
`portals` job in `browser-release.yml` packages the `browser-release-lite`
artifact; download the `poki-lite-upload` artifact
(`glob2-VERSION-poki-lite.zip`) and submit it in the Poki developer portal.

## Package

The profile injects the Poki SDK
(`https://game-cdn.poki.com/scripts/v2/poki-sdk.js`), `portal-core.js` and
`poki.js` with the policy `{externalLinks: false, onlineSignIn: false, ads: true}`. Poki blocks
external requests by default and wants outbound links through
`PokiSDK.openExternalLink()`.

## Runtime adapter

`poki.js` waits for `PokiSDK.init()`, then calls `gameLoadingFinished()` once the
main menu is ready, and `gameplayStart()`/`gameplayStop()` as a match starts and
ends, around the in-game menu and pauses
([gameplay events](web-portals.md#ad-breaks)) and as the page is hidden
([HTML5 SDK](https://developers.poki.com/guide/sdk-html5)).

Poki recommends `commercialBreak()` before every `gameplayStart()` once the player
has shown intent to continue, and lets its own system decide whether an ad plays,
so `poki.js` signals a break at every match start, the first one included, and at
every return to play from the in-game menu or a pause
([ad breaks](web-portals.md#ad-breaks)). It never signals one on the way back to
the menu and keeps no minimum gap of its own. Each break calls `gameplayStop()`,
mutes audio and holds the game (no frames, so no simulation or keyboard input),
calls `commercialBreak()`, and unmutes and calls `gameplayStart()` when its
promise settles. The game must load even when `init()` fails (ad blockers), so a
rejection only leaves the SDK unused and no break runs.

## Poki Pill

On mobile Poki overlays its Pill, 46 × 62 px on screens narrower than 1211 px
(92 × 64 px at 1211 px and wider), near the top of the game. `PokiSDK.movePill(topPercent, topPx)`
moves it only vertically, between the top and the middle of the game area; the
default is `movePill(0, 24)`
([HTML5 SDK](https://developers.poki.com/guide/sdk-html5)). The documentation does
not say where the Pill sits horizontally. Our portrait touch HUD puts the stat grid
(with the tappable game-speed cell) and the minimap across the top, 4 points from
the edges ([touch controls](../mobile/gameplay-controls.md#toolbar-and-statistics)),
so the default position may cover part of it. `poki.js` does not call `movePill`:
moving the Pill can only push it down over the map, and without knowing its
horizontal position we cannot tell whether that helps. Check where it sits in the
Poki Inspector's mobile QR mode, then decide.

## Open items

- the Pill's horizontal position over the portrait HUD (above).
- a break on every return from the in-game menu may feel heavy in a strategy
  game; Poki's system limits how often an ad actually plays. Revisit after a
  playtest.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
