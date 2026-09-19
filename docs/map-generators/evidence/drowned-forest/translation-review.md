# Drowned Forest translation review

The translation sub-agent authored and reviewed four UI labels: `Drowned Forest`, `Sandbar connections`, `Wooded neck thickness`, and `Neutral clearing size`. All 33 locale catalogs listed in `data/texts.list.txt` contain the labels, and `data/texts.keys.txt` registers them.

Locales covered: English, Arabic, Brazilian Portuguese, Catalan, Czech, German, Danish, Spanish, Esperanto, Basque, Persian, French, Greek, Hungarian, Italian, Dutch, Polish, Portuguese, Romanian, Russian, Slovenian, Slovak, Serbian, Finnish, Swedish, Turkish, Traditional Chinese, Simplified Chinese, Ukrainian, Indonesian, Vietnamese, Japanese, and Korean.

The wording review checked nearby generator terminology and the intended gameplay meanings. Translations describe sand connections as exposed sandy routes; where a literal anatomical “neck” would confuse, they describe the thickness of a tree or forest barrier. “Clearing” means a forest glade, and “neutral” denotes an unowned area. Serbian preserves the catalog’s Cyrillic convention. The generator title uses natural flooded/submerged-forest wording rather than forcing the English metaphor into every language.

The initial full translation test found 32 English-fallback failures, all for a separate unused scaffold message: `The homes are too small; use a bigger home size.` After the implementation agent confirmed that message was unused, the reviewer verified each catalog’s Git diff marked it as newly added and removed that key and its values. The four intended labels remain.

Validation: `python3 test/test_translations.py` — all 5 tests pass. These automated checks establish catalog integrity and absence of the detected English fallbacks; they do not establish native-level linguistic quality or in-game text fit.

This was an AI wording review, not a native-speaker certification. Native-speaker feedback remains useful, particularly for Basque, Esperanto, and Persian. No runtime rendering or clipping check was performed by the translation reviewer.
