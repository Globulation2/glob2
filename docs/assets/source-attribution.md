# Third-party asset attribution

Sources of files not created by the Globulation 2 project:

Font :
sans.ttf : Glob2 Sans, based on DejaVu Sans 2.26 from Ubuntu, with Droid Sans Fallback CJK outlines. See data/fonts/README.md and accompanying font licenses.

Web font (platform web app and sign-in pages only, not the game) :
Nunito, copyright 2014 The Nunito Project Authors, SIL Open Font License 1.1 (licence in platform/apps/web/public/fonts/LICENSE-Nunito.txt), from @fontsource-variable/nunito. See platform/apps/web/art/README.md.

Profile photo editor (platform web app):
[react-easy-crop](https://github.com/ValentinH/react-easy-crop), copyright 2022 Valentin Hervieu, MIT.
The license is shipped in `platform/apps/web/public/licenses/react-easy-crop.txt`.

Emoticon for alliance :
kopete http://kopete.kde.org/

Phone icon for voice chat:
KDE crystal SVG

Rotating Earth :
http://www.kde-look.org/content/show.php?content=2140
http://www.kde-look.org/usermanager/search.php?username=Amibug&PHPSESSID=3782d930be9c13d06e22580720b3ff86

Embedded JavaScript runtime:
QuickJS-NG v0.17.0 (MIT) and OpenLibm v0.8.8 (MIT/BSD/ISC/Sun notices).
See data/javascript-licenses.txt and third_party/README.md.

JSON library (platform messages):
nlohmann/json v3.12.0 (MIT). See data/json-license.txt and third_party/README.md.
Runtime image codecs:
SDL_image (zlib license) and libwebp (BSD license and patent grant).
See data/image-codec-licenses.txt for notices shipped with runtime assets.

Music (data/zik/; each set directory also holds a LICENSE.txt with full details):
- `original`: Jacques-Paul Grivaz, original Globulation 2 soundtrack.
- `woodland`: adapted from "Woodland Music - Vol 1" (Level theme) by JC Sounds, CC BY 4.0, https://opengameart.org/content/woodland-music-vol-1. Remixed from the author's stems into calm/building/combat arrangements.
- `apple-cider`: adapted from "Apple Cider" by Zane Little Music, CC0 1.0, https://opengameart.org/content/apple-cider.
- `curious-critters`: adapted from "Curious Critters" by Matthew Pablo, CC BY 3.0, https://opengameart.org/content/curious-critters. Rearranged, with added original percussion.
- `moss-lanterns`, `thistle-waltz`, `bramble-jig`, `fennel-mist`: original scores rendered with VSCO 2 Community Edition and VCSL samples (Versilian Studios, CC0 1.0).
- `glass-garden`: original score with sounds designed in Surge XT (GPL-3.0).
- `orchestral-dawn`: AI-generated audio (ACE-Step 1.5, MIT) separated with Demucs (MIT). Disclosed as AI-generated content.
How these sets are built is described in [the music pipeline guide](music-pipeline.md).
