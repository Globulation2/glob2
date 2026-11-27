# GameDistribution publication

The `portals` job in `browser-release.yml` builds the GameDistribution (Azerion)
upload ([web portal packaging](web-portals.md)). Download the
`gamedistribution-upload` artifact (`glob2-VERSION-gamedistribution.zip`) and
submit it in the GameDistribution developer dashboard.

## Package

`package-portal.py --portal gamedistribution` sets `window.GD_OPTIONS = {gameId}`
in the policy script, then loads `portal-core.js`, `gamedistribution.js` and the
GameDistribution HTML5 SDK (`https://html5.api.gamedistribution.com/main.min.js`)
in that order, because the SDK reads `GD_OPTIONS` when it loads
([SDK implementation](https://github.com/GameDistribution/GD-HTML5/wiki/SDK-Implementation)).
The policy is `{externalLinks: false, onlineSignIn: false, ads: true}`.

The game ID comes from `--game-id`, else `GLOB2_GAMEDISTRIBUTION_GAME_ID` (the
release workflow passes the `GLOB2_GAMEDISTRIBUTION_GAME_ID` repository variable of
the release mirror), else the placeholder `00000000000000000000000000000000` with a
warning. Set the variable once the game is registered and rebuild the upload.
GameDistribution documents no size or file-count limit; the 0.11.8 upload is
132.0 MB and 495 files.

## Runtime adapter

`gamedistribution.js` adds `GD_OPTIONS.onEvent`: `SDK_GAME_PAUSE` silences the
game's audio and marks gameplay paused, and `SDK_GAME_START` restores both. A
handler already in `GD_OPTIONS` still receives every event. At the natural breaks
the game reports ([ad breaks](web-portals.md#ad-breaks)) it calls
`gdsdk.showAd()` and holds the game until its promise settles or
`SDK_GAME_START` arrives. It never calls `showAd()` before the SDK announces
`SDK_READY`: a break requested earlier holds the game and waits for it, and asks
only if the break is still waiting (the 10-second start timeout releases the game
otherwise); `SDK_ERROR` ends a waiting break without an ad and no later break
asks. Unlike the other portals this includes the first match:
the SDK guide asks for a pre-roll on the start button, and activation needs it
viewed once. The pre-roll starts the 3-minute gap every ad break is followed by
([ad breaks](web-portals.md#ad-breaks)), and the SDK applies its own interval on
top.

## Open items

- register the game and set `GLOB2_GAMEDISTRIBUTION_GAME_ID`.
- GameDistribution asks for `showAd()` from inside a touch-up or mouse-up handler
  so ads work on mobile. Our breaks come from the game loop: the `start` break
  fires when the match is about to run its first tick, after the map has loaded,
  frames after the click on Play, and the page cannot tell which DOM click chose
  to play. Calling `showAd()` from that click would need the game to report the
  intent synchronously from its input handling (a C++ change), and waiting for a
  further tap would hold the player at an unexplained pause, so neither is done.
  Test the pre-roll on iOS Safari and Android Chrome inside an iframe before
  submitting.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
