# Filipino native validation evidence

Extract this archive at the same root as the other Filipino evidence archives; all paths are relative to the localization kit.

Authority: `visual/native-review.json` (SHA-256 `6054f2d63565310d2e9b37e02e275a0bd92eeb221fd4713ce7c031b53e5d7070`). The combined review audits 39 completed commands: 32 historical captures executed by producer `091b6ef5120fc0d6aa37ac9570e2cc61c9f878c2`, plus seven final commands executed by producer `22be432380b0fd45a1883530ecccb46261d28c32`. Neither historical producer nor screenshots are relabeled.

Final native catalog: `8dcf24182cd5c711f2df1ebbca435cfde2d38a78c038b447de309fd63666b57d`; online: `99ed554f753390682d437318e20ed9d7ddbd54fbe2c798df41b42d4fb3fb2446`. Catalog text and JSON-overlay hashes differ by format and are recorded separately in receipts.

- `rendering/native/`: historical desktop and forced-phone menus, settings, lobby, gameplay, dialogs, and all ten tutorial guidance captures. These show the earlier two compact captions.
- `rendering/native-final/`: intermediate final-caption captures and complete failed-before matrix. It identified 559 high-DPI width failures; this matrix is retained as failure evidence, not a passing result.
- `rendering/native-fixed/`: final six screen/game commands plus complete passing matrix after the Filipino authored-width fix. All 37,536 samples (6,256 rows × sizes 10/13/20 × scales 1/2) have glyph coverage and fit their actual drawn destinations. The 559 raw raster diagnostics remain explicitly reconciled because the complete bitmap is scaled into its authored destination.
- `visual/native-width-carry-forward.json`: all 18,768 one-times-scale sample records and all 893 corresponding PNG hashes are identical before and after the font fix. Together with the exact two-caption delta, this supports carrying unchanged historical guidance; old full screenshots still show the old Tools caption.
- `native-width-fixed-results.json`, focused log and JUnit: 16 focused language/font cases passed on final producer 22be.

All 87 non-matrix real-screen PNGs from the three capture sets are included, along with command receipts, compressed process logs, build and dependency provenance, frozen input snapshots, complete before/after matrix JSON and source/input/font hash proofs. Matrix PNGs are deliberately sampled: one final page per size/scale combination, plus the worst before/after 2× case. `published/native-omitted-matrix-pngs.json` inventories every included and omitted matrix page by SHA-256. `published/native-evidence-manifest.json` is delivered next to this archive and inventories every archived file and the archive hash.

Scope: Linux software-rendered development harness at desktop 1280×860 and forced phone presentation 390×844. No actual Android/iOS, Windows/macOS or shipping-build execution is claimed. The lobby preview is its initial generating state. Matrix escape and placeholder tokens are literal; matrix coverage does not mean every message was displayed in its runtime context. Native initial gameplay has no selected building; selected-building and long-help review belongs to the separate actual-browser evidence. The three identified native presentation findings are resolved in `visual/native-findings.json`.

No executable, library, font, artwork tree, profile directory, credential, trace or cache is distributed in this archive. Build/executable/font hashes identify the actual locally executed inputs.
