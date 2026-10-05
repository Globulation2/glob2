# Final PR749 verification

Head 70d2fdf9b5b62823befbad18ef4437a99b131e57, base 6afcdb5caa983fd389188e345f101aa7c0e73bc8. Fetched master before final acceptance: unchanged. The final PR only adds apt installation of ffmpeg to the platform CI job. Current master #747 already includes MusicStream.cpp in the unit executable and supplies the previously absent translation entries; the earlier duplicate native repair has been removed.

Ubuntu26.04.1 x86_64, Node22.22.1, Python3.14.4, ffmpeg/ffprobe8.0.1, locked npm dependencies, PostgreSQL test harness with disposable databases. Final head:
```sh
# From platform/
npm test -- apps/api/test/music.test.ts
# From repository root
/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python -m unittest discover -s test/build_system -v
python3 data/check_translations.py --strict
python3 test/test_translations.py
```
13 music API tests pass (11.53s). 312 build contracts pass (1 optional skip,55.806s). Strict translation structural check reports zero missing/obsolete keys and zero errors;5 translation tests pass. Untranslated new entries remain pending under the existing upstream policy; this repair changes no catalog or assertion.

Earlier hosted platform job on d495aad7b54d1a18e0d1a5d11eb0b236cb087ef1, base723be2c4f2056c9c0932d0fdc1abd4ba7ec3bd2c, used exactly the same apt installation step: Ubuntu24.04 x86_64, Node22, PostgreSQL16, ffmpeg package7:6.1.1-3ubuntu5, npm ci. Complete platform job passed591 tests,6 skips; lint/typecheck/web build/migrations also pass. Hosted job https://github.com/Globulation2/glob2/actions/runs/37250625661/job/111577861044 . Complete log retained here.

Limits: final refreshed hosted matrix still pending; native/Android/macOS and simulation checksums are not claimed by this CI-only dependency change. Earlier native unit verification at the prior base remains at the evidence branch root, not final-head integration evidence. No test weakening, source or simulation behavior change. Failed master logs remain retained on GitHub.
