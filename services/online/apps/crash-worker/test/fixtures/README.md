# Crash worker fixtures

`test.dmp` is a Windows x86 minidump of a deliberate crash in `test_app.exe`,
copied unchanged from rust-minidump 0.27.0 (`testdata/test.dmp`, MIT licence,
Copyright (c) 2015-2023 rust-minidump contributors).

`test_app.sym` is that release's `testdata/symbols/test_app.pdb/5A9832E5287241C1838ED98914E9B7FF1/test_app.sym`
cut down to the three functions on the crashing stack (`CrashFunction`, `main`,
`__tmainCRTStartup`) and the unwind records covering them. minidump-stackwalk
symbolicates `test.dmp` with it exactly as with the full file.
