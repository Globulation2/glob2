# Poki publication

Poki gets the small download build, not the full package, so it is not in the
`portals` release matrix yet. Poki states no size limit but asks for a small file
size and progressive loading: players leave after about ten seconds of loading
([requirements](https://developers.poki.com/guide/requirements-quality)).
`package-portal.py --portal poki` packages and verifies any `package-static.py`
output; the release wiring follows once the small build exists.

## Package

The profile injects the Poki SDK
(`https://game-cdn.poki.com/scripts/v2/poki-sdk.js`), `portal-core.js` and
`poki.js` with the policy `{externalLinks: false, onlineSignIn: false}`. Poki blocks
external requests by default and wants outbound links through
`PokiSDK.openExternalLink()`.

## Runtime adapter

`poki.js` waits for `PokiSDK.init()`, then calls `gameLoadingFinished()` once the
main menu is ready and `gameplayStart()`/`gameplayStop()` as gameplay starts and
the page is hidden ([HTML5 SDK](https://developers.poki.com/guide/sdk-html5)). The
game must load even when `init()` fails (ad blockers), so a rejection only leaves
the SDK unused.

## Open items

- the small download build and its release wiring;
- `commercialBreak()` between matches, muting audio for its duration.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
