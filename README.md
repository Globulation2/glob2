# Music Studio subprocess import repair verification

Final tested head07ce31f73 (full SHA in environment.txt), base42c7c07a51efe62bceebb36c425416704e8bc5a1. Fresh master fetched before final validation remains the same base.

Hosted run37271070460 job111640753958 failed the seeded export child with ModuleNotFoundError glob2music. Parent tests add tools/music to sys.path; this process-local change is absent in spawned Python. Child cwd now equals the actual music package root, so module lookup works independently of inherited PYTHONPATH. Seed7 equality and seed8 difference requirements unchanged; production music code unchanged.

Linux x86_64 Ubuntu26.04.1 Python3.14.4; fresh venv with exact committed tools/music/requirements.txt pins. Dependencies and ffmpeg version retained. Before: env -u PYTHONPATH tools/music/.venv/bin/python, sys.path inserts tools/music and tools/music/tests only in parent, unittest runs StudioBoundaryTests.test_seed_controls_composition_import_and_arrangement. FAIL exactly child ModuleNotFoundError; before.log retained.

Final exact commands:
python3 -m venv tools/music/.venv
tools/music/.venv/bin/python -m pip install -r tools/music/requirements.txt
env -u PYTHONPATH tools/music/.venv/bin/python -m unittest discover -s tools/music/tests -v
PASS exit0,135tests164.739s,6existingoptionalbackend skips (reasons in final.log). Includes actual native Opus roundtrip, QA, CLI/adaptation, all studio boundaries and seed subprocess runs.

Coverage: complete same suite used by hosted music CI with PYTHONPATH unset, proving the child import correction without masking it by environment setup. Optional sfizz/Surge plugin backends skipped as in hosted CI; full soundtrack renders, other OS/Python and deployment not claimed. No simulation/save/replay/network or playback behavior changes; no game-feel effect. Author accepts this focused final-head evidence under AGENTS.md. Full master recovery follows asynchronously.
