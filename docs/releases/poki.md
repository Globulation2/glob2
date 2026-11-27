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
main menu is ready, and `gameplayStart()`/`gameplayStop()` as a match starts,
ends or is paused and as the page is hidden
([HTML5 SDK](https://developers.poki.com/guide/sdk-html5)). At the natural breaks
the game reports ([ad breaks](web-portals.md#ad-breaks)), except before the first
match, it calls `gameplayStop()`, mutes and pauses the game, calls
`commercialBreak()`, and unmutes and calls `gameplayStart()` when its promise
settles. Poki's own ad timer decides whether an ad plays. The game must load even
when `init()` fails (ad blockers), so a rejection only leaves the SDK unused and
no break runs.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
