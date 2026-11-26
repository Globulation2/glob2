# Yandex Games publication

Yandex Games is not yet in the `portals` release matrix: the full package is
219.2 MB unpacked, and Yandex accepts at most 100 MB uncompressed
([requirement 1.21](https://yandex.com/dev/games/doc/en/concepts/requirements)).
`package-portal.py --portal yandex` therefore fails on the full package; it will
package the small download build once that exists.

## Package

The profile injects the Yandex Games SDK by the relative path `/sdk.js`, which
Yandex serves next to uploaded games
([SDK connection](https://yandex.com/dev/games/doc/en/sdk/sdk-about)), then
`portal-core.js` and `yandex.js`, with the policy
`{externalLinks: false, onlineSignIn: false}` (requirement 8.4.2 bans links to our
own site). It also enforces requirement 1.22: one `index.html` at the root, and no
spaces or Cyrillic in file names.

## Runtime adapter

`yandex.js` waits for `YaGames.init()`, then:

- sets the interface language from `ysdk.environment.i18n.lang`
  ([environment](https://yandex.com/dev/games/doc/en/sdk/sdk-environment)) with
  `Glob2I18n.setLocale`, the locale the shell passes to the game as its startup
  language. A language the player saved in the game still wins, as on the website;
- calls `features.LoadingAPI.ready()` when the main menu is ready and
  `features.GameplayAPI.start()`/`stop()` as gameplay starts and pauses
  ([game ready](https://yandex.com/dev/games/doc/en/sdk/sdk-gameready));
- silences audio and stops gameplay on `game_api_pause`, restoring both on
  `game_api_resume`.

## Open items

- the small download build, under 100 MB unpacked;
- the interface language must be applied before the game reads it; the SDK
  normally resolves well before saves are restored, but a slow `init()` leaves the
  browser language for that session.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
