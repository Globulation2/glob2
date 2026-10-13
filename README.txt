Resource policy foundation: native verification evidence

Tested head: d2ddb6793777eac9e8c62421f2803313e0b83b52
Tested integration base: 26715e41b177da04f068f01ca436abad5c1f95d2

The ordinary Linux x86-64 release application, unit tests and engine tests were
built with GCC 13, without fast-development, PCH or unity options. The archived
native-release-complete.json records the exact build arguments and environment.
The pinned SDL3, shader and dependency paths are reported; linked library hashes
are included separately.

170 focused compiled cases passed: one combined session case, 86 unit cases and
83 engine cases, with zero failures or skips. Nine native map requests passed:
eight generation service calls and one actual rejection by the legacy binary.
The archive contains compiled inventories, JUnit, runner logs, invocations,
map reports and generated maps, plus the combined session's checksum, replay
and save artifacts. That session case executes four variants, each with 128
steps and 48 saved-continuation steps: 704 steps in total.

These are focused native checks for this exact head. They do not establish full
cross-platform coverage, physical Android/GPU acceptance or the performance of
the combined branch. The six profile names are intents; calibrated tiers and
adaptive presentation remain unfinished. The committed match-trace assertion
passed, but its temporary runtime trace was not retained. No copied expected
trace is presented as a runtime output.

The archive hash and file inventory are in native-evidence-manifest.json.
To reproduce the selection, use the filters in the archived
final-focused-master/plan.json with test/run_tests.py, --build-dir
build/linux/client/release, --binary unit or engine, --no-display and -j1.
No fixture-update flags were used. The map invocations, fixed seeds and options
are in archived native-smoke-master/runs/*/invocation.json.
