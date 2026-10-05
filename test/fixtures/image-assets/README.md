# WebP loader fixtures

These synthetic fixtures were encoded with Pillow 12.2.0/libwebp 1.6.0.
`native-sheet.webp` is lossless Q75/method 4/exact, containing sixteen 4×2 tiles.
The four terrain atlas mip levels are Q90/method 6/exact lossy WebP. Level zero
places the decoded 16×8 frame in `LossyAlphaFixture.h` at a 64-pixel border inside
sixteen 256×256 tiles in a 4×4 grid. Independently encoded RGB differs from the
frame while alpha is exact. Lower levels use bilinear resampling.

The native sprite test verifies that this atlas is accepted, keeps every frame's
alpha and retains native logical dimensions. Test artwork is original synthetic
content under the repository's GPL-3.0-or-later license.
