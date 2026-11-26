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
The policy is `{externalLinks: false, onlineSignIn: false}`.

The game ID comes from `--game-id`, else `GLOB2_GAMEDISTRIBUTION_GAME_ID` (the
release workflow passes the `GLOB2_GAMEDISTRIBUTION_GAME_ID` repository variable of
the release mirror), else the placeholder `00000000000000000000000000000000` with a
warning. Set the variable once the game is registered and rebuild the upload.
GameDistribution documents no size or file-count limit; the 0.11.8 upload is
132.0 MB and 495 files.

## Runtime adapter

`gamedistribution.js` adds `GD_OPTIONS.onEvent`: `SDK_GAME_PAUSE` silences the
game's audio and marks gameplay paused, and `SDK_GAME_START` restores both. A
handler already in `GD_OPTIONS` still receives every event.

## Open items

- register the game and set `GLOB2_GAMEDISTRIBUTION_GAME_ID`;
- `gdsdk.showAd()` between matches, which needs a bridge from the end of a game.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
