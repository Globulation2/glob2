# Online preview fixtures

Lossless WebP copies of the map-generator illustrations referenced by
`src/ui/OnlineUIFixtures.h`. The originals remain in `docs/map-generators/`;
these fixtures exercise the same WebP-only decoder as online map previews.
Regenerate after changing an original with the pinned packaging Pillow/libwebp
runtime, converting to RGBA and encoding lossless WebP with `exact=True`.
