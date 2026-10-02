Review evidence only; do not merge this branch.
Measured source: 2895a7f3523747e8186047b090884ef026c100c2.
741 attempted cases: 740 pass, one full-registry timeout at 1800 seconds.
Native macOS ARM64, Apple Clang/LLVM 21, optimization 1.
Independently linked unit and engine profiles were kept separate.
HTML coverage and matching binaries/profiles are retained in the local artifact directory recorded in manifest.json.
Earlier O0 failure evidence is retained; the intermittent popup failure is not declared resolved.
Castor fix PR: https://github.com/Globulation2/glob2/pull/512
Coverage PR: https://github.com/Globulation2/glob2/pull/517
