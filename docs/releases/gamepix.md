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
`{externalLinks: false, onlineSignIn: false}`: the
[submission guidelines](https://partners.gamepix.com/guidelines/submission) ban
external links. The GamePix Testing Toolkit and dashboard accept zips up to 200 MB
(the published docs give no figure); the 0.11.8 upload is 132 MB and 495 files.

## Runtime adapter

`gamepix.js` reports the shell's download bar to `GamePix.lifecycle.loading`, and
calls `loaded()` and `gameStart()` once the main menu is ready. The SDK's `pause`
and `resume` events silence and restore the game's audio; both are safe to repeat,
as the [SDK documentation](https://partners.gamepix.com/sdk/doc/javascript)
requires. GamePix synchronizes only `localStorage`, so saves stay on the device.

## Open items

These wait on GamePix's answers to our questions:

- online play and accounts on our own servers;
- a file-count limit, if any (the package has about 495 files);
- an upload API, to replace the manual dashboard step;
- `GamePix.ads.interstitial()` between matches, which needs a bridge from the end of
  a game.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
