# Bastion Keys translation review

Reviewed the three new entries `[Bastion Keys]`, `[Plantation size]`, and
`[Outlying islands]` in all 33 runtime catalogs (English and 32 translations).
The final pass reread the current values and reran the checks below.

## Meaning and reviewed edits

- The generator name denotes fortified small islands or a fortress archipelago;
  none of the translations interpret “keys” as keys for locks.
- Plantation size denotes the physical size of agricultural islands. Outlying
  islands denotes peripheral or surrounding islands, used with a numeric control.
- Corrected `texts.br.txt`, whose locale is Brazilian Portuguese, from the
  mistakenly supplied Breton strings to `Ilhotas fortificadas`,
  `Tamanho das plantações`, and `Ilhas periféricas`.
- Reviewed the other labels against neighboring generator/control labels for
  meaning, script, and register. No further definite wording errors found.
- Removed the unused scaffold entry `[The homes are too small; use a bigger home
  size.]` from the catalogs and key table after confirming it was newly added in
  the working diff. It was not an active generator error requiring translation.

## Verification

Run from the repository root:

| Command | Final result |
| --- | --- |
| `python3 test/test_translations.py` | 5 tests passed |
| `python3 test/test_font_coverage.py` | 1 test passed |
| `python3 data/check_translations.py` | 0 structural errors; all 33 catalogs have 0 missing keys and 0 untranslated entries |

The audit also reports existing obsolete catalog keys; these were outside the
three-entry review and were left unchanged.

## Confidence limits

This was an independent agent wording review, not native-speaker certification
for every locale. Regional idiom and the most natural localized generator title
remain subject to native-speaker refinement. Automated font coverage verifies
available glyphs, not rendered widget fit or right-to-left presentation. The
existing unrelated `[Forts]` value in the Brazilian Portuguese catalog is Breton;
that pre-existing entry was left outside this change's scope.

## Upstream merge audit

Resolved append-location conflicts in 34 files (33 catalogs and the key table)
by preserving the complete stage 3 upstream content and appending the three
reviewed stage 2 Bastion Keys entries. Programmatic inspection confirmed that
upstream content is an unchanged prefix, each new key occurs exactly once, and
each localized value exactly matches its reviewed pre-merge value. Only these
translation files were staged; no commit was created by the translation reviewer.

Reran the same three verification commands after resolution: translation tests
5/5 passed; font coverage 1/1 passed; catalog audit reported zero structural
errors, zero missing keys, and zero untranslated entries in all 33 catalogs.
