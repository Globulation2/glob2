# Three through eight tick delays after the kernel rebase

One background worker; zero-worker control at each delay; six retained scenarios;
one warmup and two measured repetitions. Aggregate speedups use equal AI-group
weights and exclude the small-map control. Delay changes behavior, so comparisons
are against each delay's serial schedule, not against games of different lengths.

| Delay | Wall speedup | Total CPU change | Summed deadline wait across six windows |
|---:|---:|---:|---:|
| 3 | 1.225× | -1.3% | 0.494s |
| 4 | 1.148× | +2.6% | 0.618s |
| 5 | 1.195× | +1.0% | 0.415s |
| 6 | 1.189× | +0.9% | 0.437s |
| 7 | 1.193× | +0.9% | 0.233s |
| 8 | 1.164× | +3.2% | 0.163s |

The host became heavily contended during the final mixed-AI scenario: its process
CPU time roughly doubled between repetitions. Other user-owned work was not
stopped. These ratios do not establish a reliable ordering of neighboring delays.
Across the less disrupted scenarios, five through eight ticks largely plateau;
eight has the lowest summed deadline wait. Keep the already selected eight ticks,
one worker; there is no consistent evidence here for changing it. A separate
six-scenario eight-tick check immediately before this sweep measured 1.207× speedup
and +0.5% CPU; those rows are in rebase-validation.tar.gz.

All same-delay worker variants agree on ticks, outcomes and scheduling counters.
Additional exact trace checks passed for delays four through seven with zero and
one worker. This is a screening experiment, not a claim of a universal optimum.
