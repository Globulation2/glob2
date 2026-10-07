Evidence for Globulation2/glob2 PR #857 (engine snapshots and asynchronous AI), tested revision 9d5517e83.

- run_tests-all-9d5517e83.log: engine + unit suites with snapshot verification enabled (1005 engine cases passed; the single unit failure, ImageAssets 16-bit RGBA, is pre-existing on master).
- check_parallel_compute-9d5517e83.log: exact traces, replay bytes, saves and continuation at 1/2/4/8 threads.
- ab-timing-8-rounds-*.txt: paired CPU/elapsed ratios, branch vs master 644611925, six scenarios.
- ab-timing-10-rounds-before-adaptive-sharing.txt: the same measurement before adaptive worker sharing (for comparison).
- callgrind-summary.txt: Engine::run and capture instruction counts for master, the PR's previous head and the branch.
Environment: Linux x86-64 (kernel 7.0), GCC 15.2, scons release=1 server=0, pinned SDL3 prefix build.
