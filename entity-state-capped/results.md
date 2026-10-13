# PR 1168 final local verification and recovered performance

Final tested source: `95ba4eff5e962a3af036ab06d5a1a3cb725fce6a`. Integrated master: `f6f2127ab974dda82215a81782645680aa643380`.
Linux x86_64, GCC 15.2, Threadripper 2950X; native pinned SDL prefix and release
`dev_fast=0 release=1 linker=auto`, `-O3`, no PCH/unity. Original release build used
-j16; current-gradient integration and unit harness builds used -j28. Build logs,
exact commands, selected-case inventories and JUnit reports are in the archive.
Production files were unchanged between the final build and the tested commit;
embedded build revision strings are not the source authority. Binary hashes below
and the committed diff identify the actual inputs.

Selected changes: compact pointer-free unit/building records, naturally aligned
bulk capture, logical stock/performance accessors, named masks and bounded fields.
Unit performance caps at 65535, private building stock at 255; shared stock, demand,
health, hunger, speeds, timers and unrestricted continuation values remain wide.
No new runtime dependencies or exceptional stock representation. Stock caps can
limit private funding and cached-performance caps affect extreme custom content;
ordinary workload timing does not replace play review. The maintainer accepted
the selected layout and requested cleanup and merge.

Current compiled sizes (bytes):

| Record/object | Current master | Selected integrated source |
| --- | ---: | ---: |
| UnitState |360|208|
| Unit |440|288|
| UnitView |392|240|
| BuildingStateRecord |352|264|
| Building |1960|1872|
| BuildingView |408|312|

Incoming turret targeting uses existing padding differently in the compact
record, so historical 256/1864-byte building measurements are not the final sizes.
The unit state saves 152 bytes (42.2%); building state saves 88 bytes (25%).

Final engine checks: **464 cases; 0 failures; 0 skipped**.
Gradient unit checks: **59 cases; 0 failures; 0 skipped**.
The initial pre-gradient integration selection had461 passes and3 golden mismatches;
all were corrected and reverified on the final revision. The seeded-resource
trace changes include the format checksum at tick zero. JavaScript trace updates
change only aggregate checksums: all team/entity records match the previous
master goldens at every one of 256 ticks for both fixtures. Tests retain full
one/four-worker, checkpoint continuation and replay comparisons. The initial
failure reports are retained alongside successful final reports.

Final CLI continuation: same format159 game state run 512 ticks with 1/4 workers;
canonical compressed saves, replay bytes and both checksum sidecars are identical.
The engine-only input fixture needed a 36-byte empty GUI metadata suffix for the
CLI loader; the wrapped input, exact commands and hashes are retained. No game
simulation fields were altered. First diagnostic attempts failed fixture loading
and output-directory reuse; they are retained and excluded from passing claims.
Simulation revision 56, format 160, replay floor 160 and network protocol 71 mark
the cap policies plus incoming turret targeting. Saved-game floor 159 is inherited
from master; map floor 58 remains. Format 159 save loading and current continuation,
version acceptance tests and regenerated FourSquares match verification pass.

Recovered capped-layout performance was tested on`6ee39c48b113c0280f26c560ca589967d36a610c`,
based on`35b84d4ffd774742ada41d33c39ab1a50551f113`, before incoming turret/GPU work.
**No final integrated-head performance rerun is claimed.** The 120 completed release
runs compare compact stock/performance arrays against 32-bit arrays with identical
caps. Other narrowing, masks, accessors and layout grouping are common. This
isolates these arrays under the new policy, not total PR speedup against uncapped
master.10rotated pairs per fixture/worker count; 1024 warmup ticks; process CPU
includes every thread and drains pending work at both endpoints. Wall latency is
separate. 1 worker uses CPU 6; 4 workers use CPUs4–7. Raw binary/input hashes, commands,
results and representative canonical/replay/checksum files accompany the archive.

| Workload | Workers | Wide-control CPU µs/tick | Compact CPU µs/tick | CPU change [95%] | Wall change [95%] |
| --- | ---: | ---: | ---: | ---: | ---: |
| ordinary | 1 | 261.080 | 260.742 | -0.12% [-0.92, +0.62] | -0.11% [-0.91, +0.62] |
| normal-colonies | 1 | 1501.152 | 1458.767 | -2.68% [-5.06, -1.07] | -2.69% [-5.06, -1.07] |
| shared-runtime | 1 | 799.072 | 791.563 | -0.93% [-1.95, +0.26] | -0.94% [-1.97, +0.25] |
| ordinary | 4 | 330.276 | 327.093 | -1.07% [-4.61, +1.46] | -0.56% [-1.86, +0.81] |
| normal-colonies | 4 | 1618.238 | 1583.710 | -2.19% [-3.87, -0.27] | -2.05% [-3.61, -0.44] |
| shared-runtime | 4 | 1087.083 | 1059.544 | -2.83% [-6.74, -0.35] | -2.42% [-3.67, -1.06] |

All six pointwise CPU upper bounds are below the predeclared 2% practical
nonregression limit. This is workload-specific evidence; intervals are not
simultaneous coverage and selection has no independent confirmation. All 60 paired
canonical saves/final checksums match. Earlier component/layout experiments remain
historical and were not used to establish final capped performance.

Scope/omissions: final native Linux/x86_64 functional execution only. Actual
Windows/macOS/mobile/browser determinism is unavailable; CPU gradient continuation
and 1/4 worker execution are verified, not cross-platform equivalence. Rendering
performance was waived; editor and JavaScript display functionality are included
in the native suite. GPU hardware/performance and general gameplay feel are not
claimed. Historical saved matches below 159 remain rejected by incoming master's
explicit turret-targeting policy. No full hosted or release/store packaging
verification is claimed. Final-head whole-process performance remains unmeasured.

Cleanup: removed the ad hoc entity-storage benchmark harness and test-guide section;
no unselected layouts, padding controls, benchmark binaries, generated reports or
logs are shipped. Retained copy accounting and logical snapshot verification are
maintained diagnostics with focused tests.
