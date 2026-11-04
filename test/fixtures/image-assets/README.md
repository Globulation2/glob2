# Image loader fixtures

These synthetic fixtures were encoded with Pillow 12.2.0/libwebp 1.6.0.
`native-sheet.webp` is lossless Q75/method 4/exact, containing sixteen 4×2 tiles.
The four terrain atlas mip levels are Q90/method 6/exact lossy WebP. Level zero
places the decoded 16×8 frame in `LossyAlphaFixture.h` at a 64-pixel border inside
sixteen 256×256 tiles in a 4×4 grid. Independently encoded RGB differs from the
frame while alpha is exact. Lower levels use bilinear resampling.

The native sprite test verifies that this atlas is accepted, keeps every frame's
alpha and retains native logical dimensions. Test artwork is original synthetic
content under the repository's GPL-3.0-or-later license.

`terrain-hd-solid.webp` is a synthetic 128×128 lossless RGBA tile (23, 57, 91,
255). Terrain tests use it to verify compiled-page reads, partial HD packs and
fractional sampling without depending on optional production HD terrain art.

`video-group.h264` is a synthetic lossless RGB Annex B stream. It has a single
four-picture group (I–P–P–P), with SPS and PPS in the file. All four 1024×1024
frames use (17, 39, 71) except in the lower-right quadrant. Its color at
zero-based frame `i` is `(20 + 30*i, 190 - 20*i, 75 + 40*i)`. The fixture was
encoded with FFmpeg's `libx264rgb` encoder using `-g 4 -bf 0 -sc_threshold 0
-crf 0 -threads 1 -x264-params repeat-headers=1 -f h264`.
