# GamePix publication

GamePix hosts its own copy of the browser game inside an iframe on its domain. The
`portals` job in `browser-release.yml` builds the upload
([web portal packaging](web-portals.md)). Download the `gamepix-upload` artifact
(`glob2-VERSION-gamepix.zip`) and submit it on the GamePix developer dashboard.
GamePix publishes no upload API, so this step is manual until it does.

## Package

`package-portal.py --portal gamepix` (or `package-gamepix.py`) injects the
manual-lifecycle configuration (`window.GamePixConfig={lifecycle:'manual'}`), the
GamePix SDK (`https://integration.gamepix.com/sdk/v3/gamepix.sdk.js`),
`portal-core.js` and `gamepix.js` before the first script, with the policy
`{externalLinks: false, onlineSignIn: false, ads: true}`: the
[submission guidelines](https://partners.gamepix.com/guidelines/submission) ban
external links. The GamePix Testing Toolkit and dashboard accept zips up to 200 MB
(the published docs give no figure); the 0.11.8 upload is 132 MB and 495 files.

## Runtime adapter

`gamepix.js` reports the shell's download bar to `GamePix.lifecycle.loading` and
calls `loaded()` once the main menu is ready. It calls `gameStart()` once, when
real gameplay starts: the first match the player starts is playing, as the
[SDK documentation](https://partners.gamepix.com/sdk/doc/javascript) asks ("when
the first level, timer, or active gameplay loop begins"), not at the menu. The
SDK's `pause` event holds the game (no frames, so the simulation, timers and
input stop) and silences its audio, and `resume` restores both, as it does for
GameDistribution's `SDK_GAME_PAUSE`; both are flags, so repeating either does
nothing, as the documentation requires. At the natural breaks the game reports
([ad breaks](web-portals.md#ad-breaks)), except before the first match and at most
one break every three minutes (our floor for every ad portal; GamePix documents
no interval), it calls
`GamePix.ads.interstitial()` (or the legacy `GamePix.interstitialAd()`) and holds
the game until its promise resolves; any `success: false` just ends the break.
GamePix synchronizes only `localStorage`, so saves stay on the device.

## Open items

These wait on GamePix's answers to our questions:

- online play and accounts on our own servers;
- a file-count limit, if any (the package has about 495 files);
- an upload API, to replace the manual dashboard step.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
