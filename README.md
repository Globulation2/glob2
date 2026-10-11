# Mature resource oscillation validation

Source commit: `65ae86f74e335303e06c881293f1b1a99ecf946b`. Parent base: `daaef8a1ef7056a00e124166292581201fad46b3`.
Newest inspected master: `4a6af16668b5c393bbbb68e75a36c295a193e779`; intervening changes touch unrelated menu rendering only.

macOS 26.6.2 arm64, Apple LLVM 21.0.0, fast development configuration (`-O0 -g1`, GNU C++20, default Apple linker, ccache). No dependencies were changed.

104 focused cases passed on the exact source tree subsequently committed; six focused checks passed again after committing and rebuilding. Both engine-test and game builds succeeded. Simulation revision check passed.

`evidence.zip` contains build/test logs, JUnit reports, a saved game, match records/replay, per-tick checksum traces, resource and unit golden traces, and their SHA-256 manifest. `final-tests.log` and `pr-tests.log` provide compact summaries.

No Windows/Linux/browser/mobile cross-platform comparison, release performance measurement, or interactive visual playtest was performed locally. Per-tick comparisons here cover same-host workers and multiplayer clients. The PR discussion records exact commands and maintainer acceptance.
