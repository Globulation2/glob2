# Application icon

`glob2-icon.svg` is the vector master of the blue glob application icon.
`glob2-icon-1024.png` is its 1024-pixel render, the source of every generated
launcher and store icon.

## Provenance

The icon has been in the repository since the 2007 data import as PNG files of
at most 128 pixels (`content/data/icons/`). No vector or larger original is in
the repository, its history or the recovered artwork, and the 256-pixel file
added in 2026 was an enlargement of the 128-pixel one, so large icons were
blurry.

In November 2026 the icon was redrawn as vector shapes traced over
`glob2-icon-128x128.png`. It is a redraw of the same artwork, not a new design
and not the output of an image generator. The shapes were fitted to
measurements of that raster, in its pixel grid:

- the silhouette from the alpha edge: three necks and two traced blobs as
  curves through sampled edge points, the other three lobes as fitted ellipses;
- the flat fill `#0048ff`, the darker `#0041ff` shading gradients and the
  white gloss gradients from least-squares fits to the pixel values;
- the dark crescents and edge lines inside the silhouette, and the specular
  streaks, from profiles taken across each edge.

The silhouette is crisp. The crescents, edge lines, gloss caps and shadows
inside it are blurred by 0.35 to 0.65 units, about the softness the raster's
own pixels give them, so the shading reads as it always has rather than as hard
ink lines.

The rim is the one deliberate departure. The raster ends in a neutral grey
fringe about 1.1 units wide, which enlarges into a border like a sticker's. The
vector has a dark navy rim (`#141d4a`) half that wide, so it reads as a thin
dark edge on light and dark backgrounds alike.

Rendered at 128 pixels over the iOS icon's purple, the vector differs from the
raster by a mean of 5 of 255 levels across the icon's pixels and 2 of 255 away
from its edges. Because of the thinner rim the silhouette is about half a unit
smaller all round, and the two silhouettes overlap by 96%. Other known
differences: each shadow crescent is one smooth shape where the raster's varies
a little along its length, and the edge of the shadow the top-right lobe casts
on the centre body is softer.

## Generated files

`python3 platforms/mobile/icons.py` (needs Pillow) writes, from the PNG master:

- `content/data/icons/glob2-icon-256x256.png` and `-512x512.png`;
- `platforms/macos/Glob2.icns` and `platforms/windows/glob2.ico`;
- the Android `mipmap-*` launcher icons and adaptive foregrounds;
- the iOS `AppIcon.appiconset`;
- `fastlane/metadata/android/en-US/images/icon.png`.

The 16 to 128 pixel files in `content/data/icons/` were drawn for those sizes
and stay as they are; the `.icns` and `.ico` embed them unchanged.

After editing the vector, re-render the master and regenerate everything on
macOS, where `iconutil` writes the complete `.icns`:

```sh
uv run --with pillow==11.3.0 --with resvg-py==0.5.0 python platforms/mobile/icons.py --render-master
```
