Local verification

- Tested commit SHA: 4d32a09c39c2cd8eb0424d5f2bd04c5ea2d02b7d
- Base: a05d6cd8cbe594a4d281bb6e753edbe1b8d10899 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Commands: `GLOB2_SDL3_PREFIX=<pinned SDL3 3.4.16 / ttf 3.2.2 / image 3.4.6 / net 3.2.0 SDK> CCACHE=1 scons -j6 release=1 tests; python3 test/run_tests.py --binary unit --no-display --quick; python3 test/run_tests.py --binary engine --filter MarketFetch/* --filter GigRelease/* --filter HiringBucket/* --filter ResourceFetchTarget/* --filter RoundTripHungerGate/* --filter SavegameSafety/* --filter RuntimeContinuation/* --filter MapGradientInvalidation/* --filter 'TurnEngineHarness/the committed*' --filter MatchSetup/* --no-display; python3 test/check_sim_revision.py --base origin/master`
- Result: Engine and unit builds passed; 693 headless unit cases passed (13 display/slow skips); 19 focused engine cases passed; golden record regenerated; sim-version contract passed.
- Limitations: macOS arm64 only; cross-platform per-tick checks and human market gameplay not run. Stacked on draft #254 with unresolved balance feedback. Converted to draft pending those remaining checks. Earlier failing logs are retained; final tests and JUnit supersede them.
