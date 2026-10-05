# Native unit link and platform music CI repair

PR749 head d495aad7b54d1a18e0d1a5d11eb0b236cb087ef1, base723be2c4f2056c9c0932d0fdc1abd4ba7ec3bd2c. Ubuntu26.04.1 x86_64, Node22.22.1, Python3.14.4, FFmpeg/ffprobe8.0.1, managedPillow asset encoder Python; pinned SDL and embedded recording dependencies built from committed manifests. Full compiler/link flags and compiler provenance in native build/test logs. Native release C++20 O3, standard OpenGL client, no simulation source changes.

Commands from repository root:
```sh
python3 scons/sdl3_dependencies.py --prefix build/sdl3-ci/prefix --work build/sdl3-ci/sources --jobs 8
GLOB2_SDL3_PREFIX=$PWD/build/sdl3-ci/prefix scons -j8 release=1 unit-tests
python3 test/run_tests.py --binary unit --build-dir build/linux/client/release --verbose --artifacts artifacts/music-ci-unit-tests
/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python -m unittest discover -s test/build_system -v
```
Native unit executable links successfully, all752 cases pass (15 runner jobs, including738 headless cases). 312 build contracts pass,1 optionalskip.

From platform/: npm ci then npm test -- apps/api/test/music.test.ts. All13 music API tests pass with PostgreSQL test server and hostffprobe, including realuploadprocessing and retry cases.

Hosted run37250625661: GCC11/GCC13/Clangnative builds pass, completeplatformjobpasses; other fullmatrix coverage remainsinprogress. Translation audit fails on72newmusic keys absentfrom32catalogs, a pre-existingmasterregression notintroducedbythe two-line-inputrepair. It will be fixed separately. Currentmaster90dac517fdbea079171307606cd7e824200383e4 onlyadvanceswebsitepausefix748, nooverlappingbuild/dependency/testregistrychanges.

Limits: localfullengine/Windows/Android/macOS coverage notclaimed; hostedaffectedverificationrequested. Simulation/save/replay/network semantics unchanged. InitialsystemPythoncontractrun lackedPillow and failedfourmock/processassumptions; itslogisretained andmanagedrerunpasses. InitialnativeconfigurationlackedSDLextensions andthenusedanincompatiblecachedbundle; finalnativebuildusesfreshpinnedSDK instead.
