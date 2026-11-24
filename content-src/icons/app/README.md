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
- the neutral outline (`#4e4e4e`, 1.1 units outside the fills), the dark
  crescents and edge lines inside it, and the specular streaks from profiles
  taken across each edge.

Rendered at 128 pixels over the iOS icon's purple, the vector differs from the
raster by a mean of 4 of 255 levels across the icon's pixels and 2 of 255 away
from its edges, and the two silhouettes overlap by 98.6%. Known differences:
the raster's antialiased rim darkens as it fades where the vector's stays one
grey; each dark crescent is one smooth shape where the raster's varies a little
along its length; and edge lines that the raster blurs to about a pixel are
drawn at their fitted sub-pixel widths.

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
