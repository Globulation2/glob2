# OpenCL calling-thread CPU diagnostic

`GLOB2_OPENCL_API_CPU=0` is the default. It adds no diagnostic clock reads or
per-API allocations. The predictable disabled scope gate is present in this
candidate, so compare the mode-zero binary with the preceding clean binary
before interpreting small changes.

Mode `1` records guarded thread CPU intervals on the background required lane.
The fixed categories are preparation, upload, arguments, fill, kernel enqueue,
convergence read, output read, output copy and other. Preparation includes
allocation/cache/seed work, subtracting instrumented nested calls. Upload/read
categories measure the actual OpenCL calls; a blocking call includes its calling
thread CPU while waiting. Polling/flush/profiling calls and unclassified command
bookkeeping belong to other. Output copy includes transactional commit bookkeeping.
Other also contains lane queue/kernel setup and uninstrumented command release.
No device events or additional driver commands are introduced.

Mode `2` makes the same number of clock reads at the same scope entry points,
closing an empty bracket before the actual work. It reports `controlBracketNs`;
category CPU and `coveredNs` remain zero. Its call counts match mode one for the
same workload. Work remains included in the existing backend/coordinator CPU
totals. Empty brackets estimate clock/scope perturbation, not a correction that
may be subtracted to assert a speedup. Compare complete modes zero, one and two.

`coveredNs` is the valid inclusive time of lane batch scopes; exclusive category
CPU sums to it only when no scope is invalid and no reconciliation fails. The
existing backend total additionally covers wrapper eligibility/lane setup outside
these scopes. Zero/reversed endpoints invalidate their enclosing scopes; nested
time exceeding an enclosing interval or counter overflow increments reconciliation
errors. Partial valid category readings remain diagnostic and must not be called
complete. Scope calls include host phase scopes as well as instrumented APIs;
the upload/arguments/fill/kernel/read call counts each correspond to actual APIs.

Initialization latches the requested mode and marks `apiCpuConfigured` only after
successful setup. Status exports are scalar snapshots and invoke no driver calls.
Counters are calling-thread CPU, not process/driver CPU attribution. They cannot
qualify automatic promotion. Optional yielding probes decline modes one and two;
offline batch profiles must decline them until a matching configuration is bound
and independently measured. No production plan or algorithm changes accompany
this diagnostic.

Hardware-free tests cover nested conservation, unavailable/reversed clocks,
reconciliation failures, equal empty-bracket read counts and zero disabled reads.
Evidence owns all builds and real device measurements. Required protocol gates
still include exact arrays, saved/replay outputs, actual command counts and complete
process CPU/tick-tail controls; diagnostic stage times alone are not acceptance.
