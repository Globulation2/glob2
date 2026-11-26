# Game Jolt publication

The `portals` job in `browser-release.yml` builds the Game Jolt upload
([web portal packaging](web-portals.md)). Download the `gamejolt-upload` artifact
(`glob2-VERSION-gamejolt.zip`) and add it to the game page as a browser (HTML)
build; Game Jolt serves `index.html` from the zip root. Automated upload with
`gjpush` is planned.

## Package

`package-portal.py --portal gamejolt` injects only `portal-core.js` (Game Jolt
needs no SDK) with the policy `{externalLinks: true, onlineSignIn: true}`. Game
Jolt documents no size or file-count limit for browser builds
([web builds help](https://gamejolt.com/help/web-builds)), so the profile enforces
none. The 0.11.8 upload is 132.0 MB and 494 files.

## Open items

- `gjpush` upload automation from the release workflow;
- the Game Jolt Game API (trophies, scores), which needs a bridge from the game.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
