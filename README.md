# Evidence: online hub flow fixes

Tested commit 6f0dee087 on base dcc3aba5a (Linux x86_64, g++ 15.2.0, release=1 server=0, pinned SDL3 prefix).

- screenshots/compare-*: before (left, master) and after (right), mobile-gallery harness, SDL dummy video and software renderer.
- logs/full-suite-branch.log: python3 test/run_tests.py under Xvfb (695 passed, 3 failed).
- logs/master-preexisting-failures.log: the same 3 cases fail on master (EngineSession viewport, MapRenderResize fullscreen); environment, not this change.
