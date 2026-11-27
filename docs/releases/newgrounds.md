# Newgrounds publication

The `portals` job in `browser-release.yml` builds the Newgrounds upload
([web portal packaging](web-portals.md)). Download the `newgrounds-upload` artifact
(`glob2-VERSION-newgrounds.zip`) and submit it as an HTML5 game project on
Newgrounds, with `index.html` at the zip root.

## Package

`package-portal.py --portal newgrounds` injects only `portal-core.js` (Newgrounds
needs no SDK) with the policy `{externalLinks: true, onlineSignIn: false}`: links to
our site stay, and Play online is hidden. The game would reach the online service
at `app.glob2.online` from the portal's own origin, and the service sends no CORS
headers, so online play could not connect from a Newgrounds page. The profile fails uploads above 250 MB zipped. That
figure is the commonly cited project limit; the
[Newgrounds wiki](https://www.newgrounds.com/wiki/help-information/content-submission/games-and-movies)
was not readable without signing in, so confirm it on the upload form. The 0.11.8
upload is 132.0 MB and 494 files.

## Open items

- confirm the upload size limit and any file-count limit on the project form;
- the Newgrounds.io API (medals, scoreboards), which needs a bridge from the game.

[Web portals](web-portals.md) · [Release index](README.md) · [Documentation index](../README.md).
