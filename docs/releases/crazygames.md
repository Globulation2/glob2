# CrazyGames publication

The `portals` job in `browser-release.yml` builds the CrazyGames upload
([web portal packaging](web-portals.md)). Download the `crazygames-upload` artifact
(`glob2-VERSION-crazygames.zip`) and submit it in the CrazyGames developer portal.

## Package

`package-portal.py --portal crazygames` injects the CrazyGames HTML5 SDK v3
(`https://sdk.crazygames.com/crazygames-sdk-v3.js`), `portal-core.js` and
`crazygames.js` with the policy `{externalLinks: false, onlineSignIn: false}`.
The [technical requirements](https://docs.crazygames.com/requirements/technical/)
allow 250 MB in total and 1,500 files, relative paths only, and an initial download
of at most 50 MB (20 MB for the mobile homepage). The packager enforces the total
(measured unpacked, the stricter reading) and the file count; it cannot see the
initial download.

The 0.11.8 upload is 219.2 MB unpacked (132.0 MB zipped) and 495 files, within the
total and file limits. Its initial download is not: the menu needs 54.5 to 74.6 MB
uncompressed depending on the interface language, or 25.4 to 31.9 MB if CrazyGames
serves the files with gzip ([measured size](web-portals.md#measured-size)). Until
CrazyGames confirms that it compresses transfers, the upload risks rejection on the
50 MB rule, and it misses the 20 MB mobile-homepage threshold either way.

## Runtime adapter

`crazygames.js` waits for `CrazyGames.SDK.init()`, then calls
`SDK.game.loadingStart()`, `loadingStop()` when the main menu is ready, and
`gameplayStart()`. Following the [game module guide](https://docs.crazygames.com/sdk/game/),
it does not report gameplay around tab switches, and the player's
`settings.muteAudio` (read at start and through `addSettingsChangeListener`) silences
the game's audio. Signals reached before `init()` resolves are replayed after it;
a rejected `init()` leaves the game running without the SDK.

## Open items

- a smaller initial download (the small download build) to meet 50 MB uncompressed
  and the 20 MB mobile threshold;
- `gameplayStop()` around in-game pauses and menus, which needs a bridge from the
  game;
- video ads between matches (`adStarted` and `adFinished` pause and resume audio).

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
