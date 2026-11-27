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
initial download. The QA Tool measured the lite upload's initial download at
10.3 MB (2026-11-27), within both limits. Like every portal page, the upload sets
`user-select: none` (all prefixes) on `body`, which the requirements ask for on
mobile ([package](web-portals.md#package)).

The standard 0.11.8 package would be 219.2 MB unpacked, within the total and file
limits, but its menu needs 54.5 to 74.6 MB uncompressed before it is interactive,
over the 50 MB initial download ([measured size](web-portals.md#measured-size)), so
the release packages the lite edition instead.

## Runtime adapter

`crazygames.js` waits for `CrazyGames.SDK.init()`, then calls
`SDK.game.loadingStart()`, `loadingStop()` when the main menu is ready, and
`gameplayStart()`/`gameplayStop()` as a match starts and ends and around the
in-game menu and pauses ("every game break (entering a menu, ending level, pausing
the game)", [game module guide](https://docs.crazygames.com/sdk/game/)). The QA Tool's
"Load time" runs from Play Now to the first `gameplayStart()`, so it includes the
first-run questions and menus; that is intended. The
[technical requirements](https://docs.crazygames.com/requirements/technical/) want
the first gameplay start when the player "enters in a playable state, so excludes
menus", the 20-second time-to-gameplay rule applies only to externally hosted
games, and the initial download is within the 50 MB limit, so the number is
informational here. Following the [game module guide](https://docs.crazygames.com/sdk/game/),
it does not report gameplay around tab switches, and the player's
`settings.muteAudio` (read at start and through `addSettingsChangeListener`) silences
the game's audio. Signals reached before `init()` resolves are replayed after it;
a rejected `init()` leaves the game running without the SDK. Touch or mouse
controls follow `SDK.user.systemInfo.device.type` (`mobile` and `tablet` touch,
`desktop` mouse), as the technical requirements ask, instead of the shell's touch
detection ([presentation](web-portals.md#touch-or-mouse-presentation)).

At the natural breaks the game reports ([ad breaks](web-portals.md#ad-breaks)),
except before the first match, it calls `SDK.ad.requestAd('midgame', ...)`. The
game is held from the request, as the
[ad requirements](https://docs.crazygames.com/requirements/ads/) ask, audio mutes
at `adStarted`, and `adFinished` or any `adError` (`adsDisabled` throughout Basic
Launch, `adCooldown` within the SDK's 3-minute interval, `unfilled`, `adblock`)
resumes it ([video ads](https://docs.crazygames.com/sdk/video-ads/)).

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
