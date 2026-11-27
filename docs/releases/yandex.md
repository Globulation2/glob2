# Yandex Games publication

Yandex accepts at most 100 MB uncompressed
([requirement 1.21](https://yandex.com/dev/games/doc/en/concepts/requirements));
the full package is 219.2 MB unpacked and the packager refuses it, so the `portals`
job in `browser-release.yml` packages the download-size lite edition. Download the
`yandex-lite-upload` artifact (`glob2-VERSION-yandex-lite.zip`) and submit it in
the Yandex Games console.

## Package

The profile injects the Yandex Games SDK by the relative path `/sdk.js`, which
Yandex serves next to uploaded games
([SDK connection](https://yandex.com/dev/games/doc/en/sdk/sdk-about)), then
`portal-core.js` and `yandex.js`, with the policy
`{externalLinks: false, onlineSignIn: false, ads: true}` (requirement 8.4.2 bans links to our
own site). It also enforces requirement 1.22: one `index.html` at the root, and no
spaces or Cyrillic in file names.

## Runtime adapter

`yandex.js` waits for `YaGames.init()`, then:

- sets the interface language from `ysdk.environment.i18n.lang`
  ([environment](https://yandex.com/dev/games/doc/en/sdk/sdk-environment)) with
  `Glob2I18n.setLocale`, the locale the shell passes to the game as its startup
  language. A language the player saved in the game still wins, as on the website;
- calls `features.LoadingAPI.ready()` when the main menu is ready and
  `features.GameplayAPI.start()`/`stop()` as a match starts and ends and around
  the in-game menu and pauses
  ([game events](https://yandex.com/dev/games/doc/en/sdk/sdk-game-events));
- treats a window without focus like a hidden tab (requirements 1.3 and 1.19.3):
  on `blur` it calls `GameplayAPI.stop()` during a match and silences audio, and
  on `focus` restores both, so clicking into another window or the Yandex page
  around the game stops gameplay and sound. A page that loads without focus
  starts silent until the first click focuses it. The simulation itself keeps
  running while unfocused; only a hidden tab pauses it;
- silences audio and holds the game on `game_api_pause`, restoring both on
  `game_api_resume`;
- at the logical pauses the game reports (requirement 4.4,
  [ad breaks](web-portals.md#ad-breaks)), except before the first match, calls
  `adv.showFullscreenAdv` and keeps sound and gameplay paused (4.7) until
  `onClose` or `onError` ([ads](https://yandex.com/dev/games/doc/en/sdk/sdk-adv)).
  Yandex controls the frequency: a call that comes too soon closes at once.

## Open items

- the interface language must be applied before the game reads it; the SDK
  normally resolves well before saves are restored, but a slow `init()` leaves the
  browser language for that session.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
