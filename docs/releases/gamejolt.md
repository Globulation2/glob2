# Game Jolt publication

The `portals` job in `browser-release.yml` builds the Game Jolt upload
([web portal packaging](web-portals.md)), `glob2-VERSION-gamejolt.zip`, and Game
Jolt serves `index.html` from the zip root. Dispatch `browser-release.yml` with
`gamejolt` on to push it: the `gamejolt` job verifies the `gamejolt-upload`
artifact again and pushes it with the official
[`gjpush`](https://github.com/gamejolt/cli) CLI (v0.5.0, pinned by checksum) as a
browser build of game 1122762, package 1154961
([game page](https://gamejolt.com/games/globulation2/1122762)).

## Upload job

The job runs in the `gamejolt-release` environment on the release mirror, which
needs the owner's approval and holds `GJPUSH_TOKEN`, the account's Game Token (avatar
menu, then Game Token on gamejolt.com). Game Jolt release versions are semver, so the
job folds the fourth part of the tag into the patch number: `v0.11.8.1` becomes
release `0.11.801` and `v0.11.9.0` becomes `0.11.900`, which keeps releases in tag
order. A failed push can be rerun; gjpush resumes a partial upload.

## Package

`package-portal.py --portal gamejolt` injects only `portal-core.js` (Game Jolt
needs no SDK) with the policy `{externalLinks: true, onlineSignIn: true}`. Game
Jolt documents no size or file-count limit for browser builds
([web builds help](https://gamejolt.com/help/web-builds)), so the profile enforces
none. The 0.11.8 upload is 132.0 MB and 494 files.

## Open items

- the Game Jolt Game API (trophies, scores), which needs a bridge from the game.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
