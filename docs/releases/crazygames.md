# CrazyGames publication

The `portals` job in `browser-release.yml` builds the CrazyGames upload from the
download-size lite edition ([web portal packaging](web-portals.md)). Download the
`crazygames-lite-upload` artifact (`glob2-VERSION-crazygames-lite.zip`) and submit
it in the CrazyGames developer portal.

## Package

`package-portal.py --portal crazygames` injects the CrazyGames HTML5 SDK v3
(`https://sdk.crazygames.com/crazygames-sdk-v3.js`), `portal-core.js` and
`crazygames.js` with the policy `{externalLinks: false, onlineSignIn: false, ads: true}`.
The [technical requirements](https://docs.crazygames.com/requirements/technical/)
allow 250 MB in total and 1,500 files, relative paths only, and an initial download
of at most 50 MB (20 MB for the mobile homepage). The packager enforces the total
(measured unpacked, the stricter reading) and the file count; it cannot see the
initial download.

The standard 0.11.8 package would be 219.2 MB unpacked, within the total and file
limits, but its menu needs 54.5 to 74.6 MB uncompressed before it is interactive,
over the 50 MB initial download ([measured size](web-portals.md#measured-size)), so
the release packages the lite edition instead.

## Runtime adapter

`crazygames.js` waits for `CrazyGames.SDK.init()`, then calls
`SDK.game.loadingStart()`, and `loadingStop()` with the first `gameplayStart()` as
soon as the first screen is interactive (the sound choice, the introduction offer
or the main menu). The QA Tool's "Load time" runs from Play Now to that first
`gameplayStart()`; reporting it only when a match started counted the first-run
questions and the player's time in the menus (61.9 s in the QA Tool, against a
bar of about 20 s). That first gameplay lasts into the first match; from its end
on, `gameplayStop()` and `gameplayStart()` follow the matches, and the menus
between them are breaks, as the game module guide asks. The
[technical requirements](https://docs.crazygames.com/requirements/technical/) say
the first gameplay event should exclude menus when measuring the initial download;
here the initial download up to the interactive screen is the lite edition's
menu download, the figure this page's 50 MB check already uses. Following the [game module guide](https://docs.crazygames.com/sdk/game/),
it does not report gameplay around tab switches, and the player's
`settings.muteAudio` (read at start and through `addSettingsChangeListener`) silences
the game's audio. Signals reached before `init()` resolves are replayed after it;
a rejected `init()` leaves the game running without the SDK.

At the natural breaks the game reports ([ad breaks](web-portals.md#ad-breaks)),
except before the first match, it calls `SDK.ad.requestAd('midgame', ...)`. The
game is held from the request, as the
[ad requirements](https://docs.crazygames.com/requirements/ads/) ask, audio mutes
at `adStarted`, and `adFinished` or any `adError` (`adsDisabled` throughout Basic
Launch, `adCooldown` within the SDK's 3-minute interval, `unfilled`, `adblock`)
resumes it ([video ads](https://docs.crazygames.com/sdk/video-ads/)).

## Open items

- the 20 MB mobile-homepage initial download;
- `gameplayStop()` around in-game pauses.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
