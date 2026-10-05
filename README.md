# Terrain translations verification

Tested head4a738acab (full revision in PR comment), base79c8d65f52cfea0a91efa3c104f97d79fd5de54d. Refetched current master before acceptance; unchanged.
Ubuntu26.04.1x86_64, Python3.14.4, bundled sans.ttf; actual SDL3.4.16/SDL3_ttf3.2.2 from the repository-pinned SDK used by font coverage.

```
python3 -m unittest discover -s test -p test_translations.py -v
python3 data/check_translations.py --strict
GLOB2_SDL3_PREFIX=/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci/prefix python3 -m unittest discover -s test -p test_font_coverage.py -v
git diff --check
```

Before:5translationtests,192English-fallbacksubtestfailures (six entries in32catalogs). After:5tests pass; strict audit exit0,0structuralerrors,0missingkeys and no new unapproved untranslated values. Existing pending translations remain pending. One font coverage test passes across all catalog characters using SDL3_ttf, including the new values. An initial font test without the SDK environment could not locate SDL3_ttf; the final command above supplies the actual pinned library and passes.

Only six values per non-English catalog change: ice/road editor names and experiment labels/help. Half/double movement speed, one health point per32ice-exposed simulation ticks, and construction/resource restrictions preserved. Every other line is preserved, including trailing empty values. English and keys/shared-value/pending policies unchanged.

No engine/simulation/save/replay/protocol changes; noSIM_REVISION bump. Automated checks cover encoding/catalog structure/glyphs and absence of English fallbacks. No native screenshot layout check or independent native-speaker review performed; wording improvements can be made separately. Original CI failure https://github.com/Globulation2/glob2/actions/runs/37281174773/job/111679727732
Maintainer acceptance: Codex accepts focused verification for the localized terrain controls under AGENTS.md.
