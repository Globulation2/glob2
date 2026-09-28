# PR #208 cleanup evidence

Implementation revision: `29848f8898769f9c3afc3a77b3f72804fb28707d`.

This branch contains review evidence only. The implementation branch has no generated captures, logs, profiles, or validation reports.

## Checks

- 203 CppUnit cases.
- 91 newly registered keys across 33 supported catalogs; strict structural, numbered-placeholder, shared-vocabulary and font-coverage checks.
- 24 UTF-8 wrapping cases, 30 build-system checks, 15 browser unit tests and four structural checks.
- Native mobile input, responsive menu, mobile presentation, gameplay touch/editor, portable renderer and text-raster harnesses.
- Linux software-GL text-raster reproduction and correction, including fractional scales and nearby cache reuse. The original pixel-accuracy threshold is unchanged.

Final browser and hosted-CI results will be recorded after they finish.

## Captures

The gameplay harness renders the production views in German and Japanese at 320×568 and 568×320 host points. Retina captures contain twice as many pixels. Keyboard workspace captures reserve only 120 host points for the editor; the black remainder represents the simulated keyboard occlusion, not a physical keyboard capture.

The localized file-dialog fixture intentionally supplies the literal title `Save map`, a test filename, and bundled map names. Those fixture strings are not localization keys; production callers provide translated titles. Other labels and controls use the real catalogs.

`before/` preserves the initial clipped German results actions and the earlier keyboard-caption captures. Final captures show wrapping within the original action bounds. Test assertions retain action identities, chart expansion, existing editor dispatch, and unchanged game checksum/order count across localized rendering.

Native frontend fixtures use a borderless window to fit 844-point tall views on the laptop screen. Exact viewport-size assertions remain enabled.

## Scope and limits

No new physical Android/iOS or real-device IME qualification was performed for this cleanup. Previous device and full browser evidence is linked in the PR and identifies its own tested revision. Translations were authored and checked during this pass; this is not a claim of review by native speakers of every language.

The four pre-existing untracked files remain outside the commits. Reusable gallery sources, app icons, certificate fixtures and pinned SDK metadata are retained.
