# CLI 2 browser/shell fixture repair

Tested9b2947ef96a0caf3a995cd20f59c9f85588cc4c2 based on182b7f89617f2ce88ed5a180e53046c896bf2a94; master freshly fetched unchanged. Linux x86_64 Python3.14.4, Node22.22.1, repository Playwright. No production behavior changes.

Native artifact11659933861 and web artifact11660067969 from run38018550179/source69ef3. Production binaries unchanged by intervening formatting, test fixes, and Mac release script changes. Web extracted into artifacts/web-69ef-current and build/emscripten/client/release points there.

Browser command: npx --prefix browser playwright test --config=browser/playwright.config.js browser/tests/generator-studio.spec.js --grep 'frozen generator game' --project=chromium --project=firefox --project=webkit --reporter=list --output=artifacts/cli2-browser-shell/results. PASS6cases, allserial/threaded checksums and save continuation match existing native golden. All12checksumfiles uploaded with this evidence. No golden orSIMrevision edits.

Shell command: LD_LIBRARY_PATH="$PWD/build/sdl3-ci/prefix/lib" GLOB2_CLI_BINARY="$PWD/artifacts/maxima-cli2-runs/native/build/linux/client/release/src/glob2" python3 -m unittest discover -s test -p test_cli_smoke.py -k test_bash_completion_commands_enums_equals_and_paths. PASS1case including command, enum, equals and path completion assertions.

DownloadedWindowsartifact11660825380 contains completion.bash with388CRLFs. bash -n windows-completion.bash fails atline2; normalize through read_text/write_text(newline="\n") and bash -n windows-completion-lf.bash passes. NativeWindowsexecution unavailable; this is exacthostedfixture syntax reproduction plus Linuxcompletion assertions, not a Windowsrunner claim. Fullengine/native suites omitted for test-only arguments/newline changes. git diff --check passes.
