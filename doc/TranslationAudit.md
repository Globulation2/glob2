# Translation audit and completion

Worktree: `/Users/bradley/glob2-translations`; branch: `codex/translation-audit`.
Base: `origin/master` commit `88934ecfb`. Initial audit: `8b1eb8a34`.

## Completion

All **3,227 remaining required blank entries** have been translated. All 28
catalogs contain the 613 registered keys and have zero required blank values.
Intentionally empty second rows of building descriptions remain where appropriate.
The initial audit reduced 4,445 missing/blank required entries to 3,227; the team
pass completed that backlog. No English fallback was copied in to inflate coverage.
Proper names, hardware key names and natural loanwords can legitimately match English.

The incomplete-language flags have been cleared. The historical
`translations.pending.json` exception list has been deleted, and CI now runs
`check_translations.py --strict`: any required blank fails validation.

## Independent team review

Each translation group was reviewed and corrected by a different agent after the
author handed off ownership. Reviewers examined changes against the initial audit,
English text, neighboring terminology and game source usages, and applied corrections.

| Catalogs | Required entries filled | Author | Independent reviewer |
| --- | ---: | --- | --- |
| Catalan, European Portuguese, Danish, Swedish, Basque | 1,065 | translate_west | translate_slavic |
| Czech, Slovak, Slovenian, Serbian | 1,046 | translate_slavic | translate_west |
| Greek, Hungarian, Romanian, Turkish, Traditional Chinese | 1,116 | translate_east | translate_slavic |
| Additional corrections to the other 14 catalogs | — | root | translate_east |

Review included AI descriptions, tutorials, network messages, keyboard actions,
unit and building names, and the meaning of labels where they appear in the game.
The western, Slavic and eastern reviews made 54, 91 and 91 distinct corrections,
respectively; reviewers also corrected narrow controls and adjacent existing errors.
These are independent model reviews, not native-speaker playtest certification.

## Second quality pass

A further pass after `82750cb98` corrected **856 existing catalog values** and
added **56 translated values** for two new complete status messages. Corrections
include semantics, grammar, spelling, terminology and removal of leftover debug text;
they are not 856 distinct gameplay bugs.

Reviewers rotated: `translate_west` inspected all active entries in Greek,
Hungarian, Romanian, Turkish and Traditional Chinese; `translate_east` inspected
all active entries in Catalan, European Portuguese, Danish, Swedish and Basque.
`translate_slavic` re-examined the Czech, Slovak, Slovenian and Serbian catalogs
after their previous independent review. The root agent inspected targeted older
text in the other 14 languages. Slavic changes then received a fresh independent
spot-review by `translate_east`; root changes and the source edit received an
independent review by `translate_slavic`. Root also spot-checked the other groups'
key-level changes and requested corrections to Greek/Hungarian event agreement.

This broader pass found incorrect or incomplete registration recovery guidance,
reversed checkbox instructions, an unrelated name used for Greek's Language label,
wrong menu/action meanings, and significant older spelling issues. It corrected
attack alerts that wrapped a singular unit-type substitution in a plural sentence,
misleading no-growth-area labels, and Simplified Chinese building names hardcoded
with `lv0` even though they apply at every level.

`GameGUIDrawBuildingHelpers.cpp` previously joined localized fragments directly
to counts, producing missing spaces and constraining word order. It now formats
`[Units still inside: %0]` and `[Units still working: %0]` as complete localized
messages. Existing conditions and single-unit messages are preserved; legacy
fragment keys remain for compatibility. The changed C++ file passes a syntax-only
compile, and the native catalog smoke test exercises the new placeholders too.
The two new messages were measured with two-digit counts against their 76 px and
148 px budgets at 10 px. Both Chinese catalogs also passed the expanded measurements
using STHeiti Light, with no missing glyphs or measured overflows.

## Important context corrections

- `[Free]` is the health-status label; `[free]` means idle. Keys remain case-sensitive.
- `[Minimum Level To Flag]` concerns the minimum **unit** level accepted by a flag.
  `[Min required level:]` can apply to explorers as well as warriors.
- `[remember unit]` preserves assigned-unit counts for buildings into the next game
  (`GameGUIDefaultAssignManager.cpp`), not the identity of an individual unit.
- `[toggle draw accessibility aids]` toggles displayed team numbers on units and
  buildings. Voice recording and unit-path shortcuts describe toggles, not start-only
  actions or creating paths.
- `[Damaged Map]` displays the wounded-unit overlay (`OverlayAreas.cpp`), not a corrupt
  map-file error. `[concrete islands terrain]` generates separate natural islands
  (`map/generator/Generator.cpp`), not cement islands.
- `[Corn]` is wheat and `[Prune]` is a plum. Building names are consistent across
  selection actions, completion/attack events and tutorial text; Greek and Romanian
  racetracks describe running training rather than horse racing or rally driving.
- Vision-sharing descriptions now refer to sharing sight around buildings with other
  teams, not merely displaying the buildings.
- Two building-description rows must read together. A blank localized second row
  falls back to English if the English row is nonempty. The checker rejects that
  mixed-language case when the first row is translated.
- Arabic edits preserve presentation-form shaping and logical order for FriBidi.
  Legacy key spellings remain unchanged to preserve source and asset references.

## Validation

```sh
python3 data/check_translations.py --strict
python3 test/test_translations.py
python3 data/check_translations.py --json
```

The read-only validator checks table format, duplicates, missing keys, placeholder
identities and counts, English fallback, mixed-language tooltip fallback, language
metadata ordering and literal source lookups. Four regression tests pass. The default
mode can report work in progress; CI uses strict mode and permits no required blanks.

A native smoke test linked the actual `StringTable`, `FormattableString`, `Toolkit`
and file-loading components. It loaded **17,164 language/key combinations**, checked
**1,092 formatted strings**, and reported **zero failures**.

Native SDL_ttf measurements checked building descriptions (152 px in the 160 px
panel at 10 px), unit-level captions with their `3/3` fraction (128 px), action labels
(245 px at 13 px), keyboard buttons/tabs, remembered-count captions, map overlays,
and the separate-islands menu label. Reviewed text was shortened to fit these budgets.
These are font-metric checks, not screenshots of every screen in every language.

## Remaining limitations

The bundled `data/fonts/sans.ttf` has no Chinese glyph coverage. The catalogs are
complete, but a CJK-capable font is still required to render Chinese in the game;
this change does not add a font or renderer fallback. Traditional Chinese was also
measured with the system STHeiti Light CJK font, with no missing glyphs or measured
overflows. Measurements using the bundled font alone cannot establish Chinese fit.

The JSON audit exposes obsolete keys, English-identical values and residual `??`
text as review queues. These are not all missing translations: keyboard names and
proper names may be identical, and legacy unregistered entries have been retained
rather than discarding translator work. Older prose outside the reviewed changes
can still benefit from native-speaker testing.

User-authored map content and embedded scenario scripts are outside this catalog
pass. Bundled tutorial mission names are still literal English; localizing them safely
requires separate display names because mission-unlock dependencies refer to the
stored names. The source scanner covers literal lookups; dynamic building, AI and
shortcut keys were inspected in context during this work.
