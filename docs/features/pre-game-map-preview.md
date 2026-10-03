# Pre-game map previews

The map chooser, custom-game setup and online room map screens use
`MapPreview`. Drag with the left mouse button to change the center of the view.
The terrain wraps horizontally and vertically. Use the wheel to zoom (1x to 4x),
and right-click or double-click to reset both center and zoom. These operations
only change the preview; they do not move colonies or change the map that starts.

A rectangular map occupies a centered rectangle with its original proportions.
Its frame follows that rectangle; the unused layout space preserves the menu
background instead of displaying black bars or a square outer frame.
Custom setup fits the map into the full available width and height, rather than
first restricting it to a square. Its instructions are centered beneath the map,
including wrapped lines. Integer-rounded aspect fits are stable when reused, so tall maps do not acquire an extra outer border.
Terrain, drag distances, and custom-game colony markers use that same transform.
Markers are centered at their actual map positions and repeated/clipped at the
seams, rather than pushed away from map edges. Wheel zoom keeps the same map
position beneath the mouse cursor, in both directions and across wrapped seams.
Changing maps resets the view; repeated updates for the same shared thumbnail do
not. Focus loss, release outside the preview, tab changes in custom setup, and
window resizing end a drag.

The normal executable also exports this same widget through the [map CLI](../map-generators/CLI.md).
PNG exports show the full map without transitions, with 2× retained-pixel scale by
default and optional 4×/8× scales; these options do not change in-game zoom limits.

## Pixels and compatibility

`MapThumbnail` retains immutable RGB pixels at up to 512 pixels on its longest
axis. Smaller maps keep one pixel per map cell. Larger maps use half-open box
averages, without double-counting boundary cells or sampling the next torus
period. The established terrain/resource palette is retained.

The existing thumbnail envelope is unchanged: signed 16-bit map dimensions,
32-bit compressed length, and the compressed bytes. The first zlib stream still
expands to the legacy 128x128, column-major, letterboxed RGB image. New encoders
may append an `MPV2` member *inside* that same compressed byte field: four magic
bytes, two big-endian unsigned 16-bit image dimensions, then a second zlib
stream containing tightly packed row-major RGB pixels. Legacy `uncompress`
reads the first stream and ignores the trailing member. New decoders accept
both legacy-only and extended thumbnails.

The entire compressed field is capped at 60,000 bytes, a limit kept from the YOG
lobby's map-thumbnail message (removed at the M9 cutover). If detailed pixels do
not fit, the encoder halves their resolution until they fit, or keeps only the
legacy image. No save, replay or simulation changes are required by this
compatible thumbnail extension.
The codec harness exercises the old uncompress call on new payloads.

Decoding uses checked binary reads and bounded allocation, validates dimensions,
and requires the exact decompressed byte count. A failed map read or decode
leaves an empty thumbnail with zero dimensions, never uninitialized pixels.

## Responsiveness and online states

Randomize keeps the previous preview, frame, and instructions visible while the
replacement is generated, without flashing a loading message. The old preview
retains its fairness and score until the replacement map and scores are ready. It
is not interactive during generation and is not considered a ready launch
snapshot. Terrain and colony markers cross-fade together over 200 ms when the
replacement arrives; the first image fades in from the placeholder. When no
previous image exists, the placeholder follows the requested map proportions.
Generation failures still display their error message.

The landscape-selection grid uses the same `MapPreview` widget as the lobby and
online screens for terrain, centered colony markers, and aspect fitting. Its
images are passive: swipes and the mouse wheel scroll the grid, including when
the gesture starts over an image. Tapping an image uses the same selection and
confirmation behavior as the rest of its card. The main custom-game preview
keeps its drag and zoom controls. Old images and map metadata remain visible
during regeneration, without loading-text flashes. Grid
images appear immediately, without fades, including when scrolling to a map that
finished generating off-screen. Completed off-screen results retain CPU pixels;
rendering surfaces are created only when their cards enter the viewport. Only
ready results can supply a chosen seed.
Click or drag an image to select and inspect it; use its label, Enter, or the Use
button to confirm. The wheel scrolls the grid, including over images. Right-click or double-click an image resets its view.

Local thumbnail lookup retains at most 16 images (at most 12 MiB of RGB pixels),
keyed by resolved path, mtime and size. Custom setup retains eight map summaries
(at most 6 MiB of terrain pixels), including header and starting colonies. It
rasterizes the already-loaded Game instead of loading Map again. Generated maps
are still saved as the exact launch snapshot; previewing reads back only the
finalized header and rasterizes the generated world, avoiding two body reloads.

On native builds the landscape picker uses one background worker, leaving more
CPU time and memory bandwidth for input and rendering. Its queue starts after the
first layout and prioritizes cards nearest the viewport center, updating as the
view scrolls, resizes, sorts, or filters. Filtered-out cards follow those still
shown. Reordering does not change a request or its seed. Candidate selection
keeps its separate worker budget. Browser builds use the same priority queue,
poll one generation attempt at a time for cards in view, and leave an
input/rendering interval between attempts; interaction postpones the next
attempt. A single browser generation attempt still runs synchronously and can
pause input until it finishes. Final generation of the chosen seed, snapshot
writing, and the first uncached Game load also remain synchronous. The picker
uses the same tightly packed terrain format, with cached viewport rasters for
both software and OpenGL rendering.

## Regression and visual review

From the repository root:

```sh
scons -j6 release=1 server=0 engine-tests
python3 test/run_tests.py --filter 'MapPreview/*'
python3 test/run_tests.py --filter 'CustomGameSetup/*'
```

The `[display]` case opens an 800x600 software window and writes native BMP
captures and selection timings to its artifact directory; the runner opens an Xvfb
screen on headless Linux and checks that no case rewrites the profile's
preferences. Core tests cover positive
and negative wraparound, rectangular coordinates, legacy/new wire images,
truncated and corrupt data, and noisy-map frame bounds. Linux CI runs the core
and visual harnesses; Windows CI runs the core harness. The existing custom-game regression checks generated-map
snapshot continuity and the surrounding setup flow.

A maintainer should try dragging across both seams, zooming around colonies,
resetting the view, browsing maps rapidly, and using a slow/offline map service.
This improves pre-game inspection without changing in-game pacing or balance.

Generated captures, map fixtures, logs and baseline reproductions belong in the
ignored `artifacts/map-preview/` workspace or in pull-request attachments.
