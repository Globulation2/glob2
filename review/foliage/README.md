# Connected foliage review evidence

Code commit: `53110681a31d08423568afe26d5b8d8f9ac45dd1`. Tested base: `ddfa81a405e0c47442e1787ccdf7d4e95ca57d6e`.

This branch holds generated review evidence only. These files are not part of the code PR.

- `final-gallery.png`: native renderer; all corner masks, narrow strips, filled patches, holes, trees, wrapping and mixed hedge/thicket.
- `sprite-comparison.png`: old artwork on the left; connected canopy artwork on the right. This isolates sprites over grass, rather than reproducing the full ground compositor.
- `ground-before-fix.png` and `ground-after-fix.png`: removal of the spiky underlay and duplicate contact shadow.
- `validation.json`: source attestation, commands, environment, coverage and limitations.

The broad native run passed 52 of 53 cases. Its only failure was the intentional reviewed geometry fingerprint change for the new smooth underlay. The baseline was updated after rendered review, and that case passed separately. All 53 selected cases are therefore covered. Six artwork tests, three catalogue tests and packaging also passed. Test logs retain the initial baseline mismatch rather than concealing it.

Validation used macOS arm64, Apple clang 21, pinned Pillow 12.2, and a fast development build. Other platforms and release performance were not tested. Two independent visual reviewers checked the final native gallery and found no remaining visual blocker. Large-area repetition and the abrupt hedge/thicket colour transition remain optional polish. Maintainer play review/acceptance has not been recorded.
