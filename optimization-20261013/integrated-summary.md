# Turret optimization: whole-engine results

Ten paired seeds, one warmup per cell, rotated original/shadow/cached ordering. Release GCC13 Linux x86_64. Positive change means slower/more CPU. All samples retained. CPU-ledger runs are separate from uninstrumented timing.

| Variant | L | Workload | Participants | Original µs/tick | Cached µs/tick | Mean change (95% CI) | Process CPU change | Workload gate |
|---|---:|---|---:|---:|---:|---|---:|---|
| combined | 4 | empty100 | 1 | 532.5 | 457.0 | -14.2% [-14.8, -13.6] | -14.2% | pass |
| combined | 4 | empty100 | 2 | 371.0 | 376.6 | +1.5% [+0.8, +2.3] | -12.6% | FAIL |
| combined | 4 | empty100 | 4 | 270.9 | 203.7 | -24.8% [-28.1, -22.2] | -14.0% | pass |
| combined | 4 | empty100 | 8 | 324.8 | 244.3 | -24.8% [-26.5, -23.1] | -12.4% | pass |
| combined | 4 | combat100 | 1 | 843.8 | 845.3 | +0.2% [-0.1, +0.4] | +0.2% | pass |
| combined | 4 | combat100 | 2 | 658.3 | 650.9 | -1.1% [-3.6, +0.6] | +0.1% | pass |
| combined | 4 | combat100 | 4 | 462.6 | 471.2 | +1.9% [+1.0, +2.9] | +0.5% | pass |
| combined | 4 | combat100 | 8 | 499.6 | 502.9 | +0.7% [-0.9, +2.2] | -0.9% | pass |
| combined | 4 | ordinary4 | 1 | 403.9 | 404.8 | +0.2% [-0.3, +0.7] | +0.2% | pass |
| combined | 4 | ordinary4 | 2 | 356.7 | 357.6 | +0.3% [+0.0, +0.5] | +0.7% | pass |
| combined | 4 | ordinary4 | 4 | 170.3 | 170.8 | +0.3% [-0.8, +1.4] | -0.1% | pass |
| combined | 4 | ordinary4 | 8 | 207.7 | 226.7 | +9.2% [-0.5, +23.6] | +2.3% | FAIL |
| combined | 8 | empty100 | 1 | 531.3 | 456.7 | -14.0% [-14.5, -13.6] | -14.1% | pass |
| combined | 8 | empty100 | 2 | 369.2 | 375.5 | +1.7% [+1.2, +2.2] | -12.4% | FAIL |
| combined | 8 | empty100 | 4 | 262.2 | 202.3 | -22.9% [-24.6, -21.1] | -11.6% | pass |
| combined | 8 | empty100 | 8 | 329.3 | 242.3 | -26.4% [-28.4, -24.6] | -12.5% | pass |
| combined | 8 | combat100 | 1 | 842.3 | 846.1 | +0.5% [+0.1, +0.8] | +0.5% | pass |
| combined | 8 | combat100 | 2 | 658.5 | 656.8 | -0.3% [-1.9, +1.1] | +0.5% | pass |
| combined | 8 | combat100 | 4 | 466.0 | 469.5 | +0.7% [-0.6, +2.0] | -0.5% | pass |
| combined | 8 | combat100 | 8 | 499.1 | 510.5 | +2.3% [+0.9, +3.6] | +0.0% | pass |
| combined | 8 | ordinary4 | 1 | 403.9 | 404.0 | +0.0% [-0.9, +0.8] | +0.0% | pass |
| combined | 8 | ordinary4 | 2 | 401.9 | 378.3 | -5.9% [-12.4, +0.3] | -5.0% | pass |
| combined | 8 | ordinary4 | 4 | 173.2 | 174.7 | +0.8% [-2.6, +5.3] | -0.6% | pass |
| combined | 8 | ordinary4 | 8 | 192.4 | 200.4 | +4.2% [+2.4, +6.2] | +4.1% | FAIL |

CPU breakdown at four participants (µs/tick): shared capture is charged only as its paired difference from original. This includes requirements inspection; common capture is not charged to targeting. Planning sums all executor threads. Publication excludes joins; wait is elapsed time, separate. Each probe has overhead.

| Variant | L | Workload | Original scans | Incremental capture | Source/facts | Index | Planning | Publication | Consumption | Admission overhead | Cached turret total |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| combined | 4 | empty100 | 151.04 | 0.64 | 7.81 | 2.90 | 6.77 | 7.22 | 57.39 | 3.51 | 86.24 |
| combined | 4 | combat100 | 9.11 | 0.07 | 3.77 | 2.89 | 5.31 | 3.30 | 3.61 | 1.77 | 20.72 |
| combined | 4 | ordinary4 | 6.85 | 1.17 | 1.92 | 3.17 | 1.86 | 3.01 | 2.58 | 2.68 | 16.39 |
| combined | 8 | empty100 | 159.18 | 1.53 | 8.29 | 2.88 | 6.81 | 7.62 | 58.85 | 3.60 | 89.57 |
| combined | 8 | combat100 | 9.15 | -0.41 | 4.02 | 2.89 | 5.19 | 3.29 | 3.58 | 1.76 | 20.32 |
| combined | 8 | ordinary4 | 7.49 | 0.95 | 1.95 | 3.15 | 1.84 | 3.05 | 2.55 | 2.62 | 16.12 |
