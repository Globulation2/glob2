# Gauntlet translation review

Re-audited all 33 language catalogs after adding the generator name, partition-thickness control, and map-capacity refusal. The localized names describe an arena of trials or challenges rather than a glove. Refusal translations consistently explain that every colony needs two defended entrances. Existing court-size and starting-tower labels remain suitable. Corrected the Brazilian Portuguese gate-width label, which previously contained Breton text.

From the repository root:

```sh
python3 data/check_translations.py --strict > docs/artifacts/gauntlet/translations/strict-audit.log 2>&1
python3 test/test_translations.py -v > docs/artifacts/gauntlet/translations/tests.log 2>&1
```

[Strict audit log](translations/strict-audit.log): zero untranslated entries, missing keys, or structural errors. Existing obsolete catalog entries are reported but unchanged.

[Verbose test log](translations/tests.log): all five tests passed:

- `test_audit_finds_missing_key_and_bad_placeholder_in_context`
- `test_catalogs_do_not_reintroduce_english_fallbacks`
- `test_duplicate_values_are_reported_without_rewriting`
- `test_missing_value_is_not_an_intentional_empty_value`
- `test_placeholders_preserve_identity_and_multiplicity`

Automated checks establish catalog integrity and absence of English fallbacks; idiomatic wording was separately reviewed by the translation sub-agent.
