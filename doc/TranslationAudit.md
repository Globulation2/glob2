# Translation audit

Audit base: `origin/master`, commit `88934ecfb` (8 September 2026).
Worktree: `/Users/bradley/glob2-translations`; branch: `codex/translation-audit`.

## Result and limits

All 28 language catalogs have been checked for structural correctness and literal
translation lookups in the C++ source. Every catalog now contains all 611
registered keys; 255 missing entries have been added. This change adds
1242 nonempty translations and corrects 378 existing values.

This is **not a completed translation of every language**. Required blank entries
fell from 4445 to 3227, across 14 unfinished languages. Their exact keys
are in `data/translations.pending.json`. These entries still use the existing
English fallback. Brazilian Portuguese and Italian now have no required blank
entries, so their incomplete markers have been cleared.

The contextual review covered all building descriptions, resource terminology,
health status, minimum unit level, speed controls, and the new error/AI strings.
It also covered missing shortcut labels, replay controls, Italian's remaining
blank entries, and obvious English text left in otherwise populated catalogs.
Other existing prose has not received an exhaustive native-speaker review.
Zero blank entries is a coverage measurement, not a guarantee of linguistic accuracy.

## Context corrections

- `[Free]` labels healthy units in the statistics panel; `[free]` labels idle
  units. The keys are case-sensitive and must not be merged.
- `[Minimum Level To Flag]` selects the minimum **unit** level accepted by a
  flag, not a level of the flag itself. Its caption must also leave room for
  the displayed `3/3` fraction.
- `[Corn]` is wheat and `[Prune]` is a plum in the game's resource vocabulary.
  Corrected maize, raisins, dried fruit, and the Greek pruning-action translation.
- Building descriptions are two separate lines. Fixed crossed Italian barracks
  and tower lines, repeated French sentence fragments, Danish flags described as
  attacking units, Simplified Chinese level-label placeholders, unfinished Serbian
  descriptions, and misleading clearing-flag descriptions.
- An empty translated second line falls back to English when the English second
  line is nonempty. Descriptions needing two English lines remain split into two
  localized lines to avoid mixing languages.
- Duplicate entries were removed using the loader's existing last-value-wins
  behavior, with explicit corrections to health abbreviations. Legacy key
  spellings remain stable to preserve source and asset references.
- The map-download failure screen now requests the registered `lost connection`
  key, which already has translations, instead of displaying an unknown key.
- Arabic additions preserve the catalog's presentation-form shaping and logical
  order for the renderer's FriBidi handling. Other Arabic text was not reshaped.

## Coverage by catalog

“Before” and “remaining” count missing or blank required values, excluding optional
second tooltip lines. “Added” also includes newly supplied optional second lines.

| Catalog | Before | Remaining | Added | Corrected |
| --- | ---: | ---: | ---: | ---: |
| ar | 9 | 0 | 9 | 11 |
| br | 27 | 0 | 27 | 25 |
| ca | 323 | 246 | 78 | 20 |
| cz | 351 | 274 | 84 | 14 |
| de | 9 | 0 | 9 | 5 |
| dk | 247 | 186 | 61 | 25 |
| en | 0 | 0 | 0 | 14 |
| es | 9 | 0 | 9 | 7 |
| eo | 9 | 0 | 9 | 7 |
| eu | 250 | 188 | 62 | 13 |
| fa | 9 | 0 | 9 | 7 |
| fr | 5 | 0 | 5 | 20 |
| gr | 159 | 139 | 20 | 20 |
| hu | 354 | 275 | 80 | 7 |
| it | 131 | 0 | 131 | 9 |
| nl | 9 | 0 | 9 | 3 |
| pl | 9 | 0 | 9 | 3 |
| pt | 335 | 257 | 78 | 10 |
| ro | 352 | 274 | 79 | 9 |
| ru | 9 | 0 | 9 | 8 |
| si | 349 | 272 | 79 | 7 |
| sk | 303 | 226 | 81 | 6 |
| sr | 353 | 274 | 86 | 15 |
| fi | 9 | 0 | 9 | 41 |
| sv | 249 | 188 | 62 | 4 |
| tr | 317 | 240 | 77 | 21 |
| zh-tw | 250 | 188 | 62 | 17 |
| zh-cn | 9 | 0 | 9 | 30 |

## Validation and maintenance

Run from any directory:

```sh
python3 data/check_translations.py
python3 test/test_translations.py
python3 data/check_translations.py --json
python3 data/check_translations.py --strict
```

The default audit is read-only. It fails on malformed tables, duplicate keys,
missing registered keys, invalid or mismatched placeholder identities, missing
English fallback, mixed-language tooltip fallback, mismatched incomplete-language
ordering, literal source lookups for unknown keys, or newly blank translations
outside the explicitly recorded historical backlog. CI runs this audit and its
regression tests. `--strict` also fails on all historical untranslated entries;
it is expected to fail until the backlog is completed. Do not expand the backlog
to hide new regressions. Remove entries from it as their translations are completed.

The JSON audit also reports obsolete keys, English-identical values, and `??`
placeholder text as review queues. Identical names and keyboard labels can be
legitimate translations, so these are not automatically rejected or rewritten.
Old unregistered entries have been retained to avoid discarding translator work.

Building descriptions were measured using the shipped `data/fonts/sans.ttf` at
10 px against a 152 px text budget inside the 160 px game panel. Minimum-unit-level
captions were measured with the appended fraction against their 128 px editor
area. These are static font-metric checks, not screenshots of every language in a
running game; customized fonts and full RTL rendering still need visual review.

The source scan covers literal `getString` calls. Dynamic building and AI lookups
were inspected separately. User-authored map content and embedded scenario scripts
are not translated by this catalog pass. The bundled tutorial campaign also has
literal English mission names; localizing those safely needs separate display names
because mission unlock dependencies currently refer to those names.
