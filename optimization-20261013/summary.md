# Turret optimization: whole-engine results

Ten paired seeds, one warmup per cell, rotated original/shadow/cached ordering. Release GCC13 Linux x86_64. Positive change means slower/more CPU. All samples retained. CPU-ledger runs are separate from uninstrumented timing.

| Variant | L | Workload | Participants | Original µs/tick | Cached µs/tick | Mean change (95% CI) | Process CPU change | Workload gate |
|---|---:|---|---:|---:|---:|---|---:|---|
| baseline | 4 | empty100 | 1 | 561.2 | 488.5 | -13.0% [-13.7, -12.2] | -13.0% | pass |
| baseline | 4 | empty100 | 2 | 398.5 | 406.2 | +1.9% [+1.2, +2.5] | -10.8% | FAIL |
| baseline | 4 | empty100 | 4 | 270.9 | 206.3 | -23.8% [-24.9, -22.6] | -12.4% | pass |
| baseline | 4 | empty100 | 8 | 333.2 | 251.9 | -24.4% [-25.5, -23.1] | -11.3% | pass |
| baseline | 4 | combat100 | 1 | 850.7 | 914.7 | +7.5% [+6.9, +8.0] | +7.5% | FAIL |
| baseline | 4 | combat100 | 2 | 650.9 | 704.5 | +8.2% [+6.9, +9.7] | +10.2% | FAIL |
| baseline | 4 | combat100 | 4 | 464.4 | 488.2 | +5.1% [+2.5, +7.5] | +7.9% | FAIL |
| baseline | 4 | combat100 | 8 | 498.0 | 533.0 | +7.0% [+5.4, +9.4] | +6.1% | FAIL |
| baseline | 4 | ordinary4 | 1 | 432.6 | 434.7 | +0.5% [+0.2, +0.8] | +0.5% | pass |
| baseline | 4 | ordinary4 | 2 | 387.7 | 389.6 | +0.5% [-0.2, +1.3] | +0.7% | pass |
| baseline | 4 | ordinary4 | 4 | 174.9 | 176.3 | +0.8% [-0.1, +1.9] | +0.5% | pass |
| baseline | 4 | ordinary4 | 8 | 191.9 | 196.7 | +2.5% [+1.2, +4.0] | +2.6% | pass |
| baseline | 8 | empty100 | 1 | 561.4 | 489.0 | -12.9% [-13.5, -12.3] | -12.9% | pass |
| baseline | 8 | empty100 | 2 | 403.8 | 404.9 | +0.3% [-2.9, +2.3] | -11.1% | FAIL |
| baseline | 8 | empty100 | 4 | 269.6 | 208.5 | -22.7% [-24.5, -21.0] | -11.4% | pass |
| baseline | 8 | empty100 | 8 | 339.2 | 252.4 | -25.6% [-26.6, -24.6] | -11.6% | pass |
| baseline | 8 | combat100 | 1 | 849.7 | 913.4 | +7.5% [+6.9, +8.1] | +7.5% | FAIL |
| baseline | 8 | combat100 | 2 | 652.4 | 698.2 | +7.0% [+6.3, +7.8] | +9.3% | FAIL |
| baseline | 8 | combat100 | 4 | 458.9 | 491.2 | +7.0% [+5.9, +8.3] | +7.9% | FAIL |
| baseline | 8 | combat100 | 8 | 491.0 | 525.7 | +7.1% [+5.1, +9.4] | +5.8% | FAIL |
| baseline | 8 | ordinary4 | 1 | 436.6 | 441.5 | +1.1% [+0.3, +1.9] | +1.1% | pass |
| baseline | 8 | ordinary4 | 2 | 389.1 | 391.3 | +0.6% [+0.1, +1.0] | +1.2% | pass |
| baseline | 8 | ordinary4 | 4 | 173.8 | 178.6 | +2.8% [+1.1, +4.5] | +1.8% | pass |
| baseline | 8 | ordinary4 | 8 | 191.9 | 199.0 | +3.7% [+2.3, +4.9] | +2.7% | FAIL |
| combined | 4 | empty100 | 1 | 563.7 | 488.9 | -13.3% [-13.8, -12.7] | -13.3% | pass |
| combined | 4 | empty100 | 2 | 396.2 | 407.7 | +2.9% [+2.1, +3.9] | -10.9% | FAIL |
| combined | 4 | empty100 | 4 | 271.3 | 207.1 | -23.7% [-25.0, -22.3] | -12.5% | pass |
| combined | 4 | empty100 | 8 | 336.3 | 246.6 | -26.7% [-27.9, -25.6] | -11.7% | pass |
| combined | 4 | combat100 | 1 | 838.8 | 844.0 | +0.6% [+0.4, +0.9] | +0.6% | pass |
| combined | 4 | combat100 | 2 | 651.4 | 653.3 | +0.3% [-0.5, +1.1] | +1.2% | pass |
| combined | 4 | combat100 | 4 | 455.2 | 470.1 | +3.3% [+2.7, +3.9] | +1.7% | pass |
| combined | 4 | combat100 | 8 | 503.2 | 509.9 | +1.3% [-0.3, +3.1] | -0.8% | pass |
| combined | 4 | ordinary4 | 1 | 430.4 | 434.1 | +0.9% [+0.4, +1.3] | +0.9% | pass |
| combined | 4 | ordinary4 | 2 | 389.8 | 394.7 | +1.3% [-0.1, +3.1] | +1.2% | pass |
| combined | 4 | ordinary4 | 4 | 174.8 | 178.0 | +1.8% [+0.7, +2.9] | +1.1% | pass |
| combined | 4 | ordinary4 | 8 | 196.9 | 199.8 | +1.5% [-0.3, +3.2] | +1.7% | pass |
| combined | 8 | empty100 | 1 | 558.2 | 481.8 | -13.7% [-14.1, -13.3] | -13.7% | pass |
| combined | 8 | empty100 | 2 | 396.4 | 405.6 | +2.3% [+1.5, +3.3] | -11.7% | FAIL |
| combined | 8 | empty100 | 4 | 269.4 | 214.9 | -20.2% [-23.8, -14.3] | -11.5% | pass |
| combined | 8 | empty100 | 8 | 325.5 | 242.1 | -25.6% [-26.3, -25.1] | -10.7% | pass |
| combined | 8 | combat100 | 1 | 843.9 | 843.5 | -0.0% [-1.3, +0.7] | -0.0% | pass |
| combined | 8 | combat100 | 2 | 649.8 | 653.5 | +0.6% [-0.9, +2.8] | +1.4% | pass |
| combined | 8 | combat100 | 4 | 457.2 | 464.1 | +1.5% [+0.9, +2.2] | +0.4% | pass |
| combined | 8 | combat100 | 8 | 498.8 | 506.1 | +1.5% [-0.1, +2.9] | -1.2% | pass |
| combined | 8 | ordinary4 | 1 | 428.2 | 430.2 | +0.5% [+0.0, +0.8] | +0.5% | pass |
| combined | 8 | ordinary4 | 2 | 387.7 | 389.2 | +0.4% [-0.1, +0.9] | +1.1% | pass |
| combined | 8 | ordinary4 | 4 | 176.9 | 180.2 | +1.8% [-0.2, +4.2] | +1.5% | pass |
| combined | 8 | ordinary4 | 8 | 197.9 | 198.9 | +0.5% [-1.4, +2.4] | +1.7% | pass |
| forecast | 4 | empty100 | 1 | 553.7 | 483.4 | -12.7% [-13.3, -12.1] | -12.7% | pass |
| forecast | 4 | empty100 | 2 | 399.6 | 405.8 | +1.6% [+0.8, +2.1] | -11.1% | FAIL |
| forecast | 4 | empty100 | 4 | 302.2 | 242.3 | -19.8% [-23.5, -16.5] | -12.9% | pass |
| forecast | 4 | empty100 | 8 | 328.9 | 246.2 | -25.2% [-26.5, -23.8] | -10.8% | pass |
| forecast | 4 | combat100 | 1 | 845.5 | 852.1 | +0.8% [+0.5, +1.0] | +0.8% | pass |
| forecast | 4 | combat100 | 2 | 652.4 | 689.8 | +5.7% [-0.4, +17.4] | +3.7% | FAIL |
| forecast | 4 | combat100 | 4 | 507.8 | 513.0 | +1.0% [-1.7, +3.8] | +0.9% | pass |
| forecast | 4 | combat100 | 8 | 506.0 | 517.1 | +2.2% [-1.0, +6.2] | +0.0% | pass |
| forecast | 4 | ordinary4 | 1 | 438.8 | 442.5 | +0.8% [+0.3, +1.6] | +0.8% | pass |
| forecast | 4 | ordinary4 | 2 | 383.4 | 385.1 | +0.4% [-0.1, +1.1] | +1.1% | pass |
| forecast | 4 | ordinary4 | 4 | 198.0 | 206.8 | +4.5% [-2.0, +12.1] | +2.6% | FAIL |
| forecast | 4 | ordinary4 | 8 | 198.3 | 207.5 | +4.7% [-0.6, +12.5] | +2.9% | FAIL |
| forecast | 8 | empty100 | 1 | 564.8 | 484.8 | -14.2% [-14.9, -13.4] | -14.2% | pass |
| forecast | 8 | empty100 | 2 | 395.9 | 406.6 | +2.7% [+1.5, +4.7] | -9.7% | FAIL |
| forecast | 8 | empty100 | 4 | 265.1 | 205.1 | -22.6% [-24.0, -21.4] | -11.6% | pass |
| forecast | 8 | empty100 | 8 | 333.2 | 251.6 | -24.5% [-26.0, -23.0] | -10.8% | pass |
| forecast | 8 | combat100 | 1 | 841.2 | 846.7 | +0.6% [+0.2, +1.2] | +0.6% | pass |
| forecast | 8 | combat100 | 2 | 652.5 | 652.2 | -0.0% [-0.7, +0.7] | +0.7% | pass |
| forecast | 8 | combat100 | 4 | 464.2 | 477.1 | +2.8% [-0.0, +5.7] | +1.3% | pass |
| forecast | 8 | combat100 | 8 | 500.4 | 517.3 | +3.4% [+2.1, +4.8] | +0.1% | pass |
| forecast | 8 | ordinary4 | 1 | 435.1 | 441.1 | +1.4% [-0.1, +2.8] | +1.4% | pass |
| forecast | 8 | ordinary4 | 2 | 384.8 | 384.6 | -0.1% [-0.7, +0.5] | +0.3% | pass |
| forecast | 8 | ordinary4 | 4 | 178.0 | 179.3 | +0.7% [-1.7, +3.2] | +0.9% | pass |
| forecast | 8 | ordinary4 | 8 | 198.1 | 199.1 | +0.5% [-2.1, +3.3] | +2.1% | pass |
| preparation | 4 | empty100 | 1 | 555.8 | 482.4 | -13.2% [-13.5, -12.9] | -13.2% | pass |
| preparation | 4 | empty100 | 2 | 397.2 | 404.6 | +1.9% [+1.4, +2.4] | -12.1% | FAIL |
| preparation | 4 | empty100 | 4 | 267.0 | 204.6 | -23.4% [-24.7, -22.0] | -12.0% | pass |
| preparation | 4 | empty100 | 8 | 339.4 | 252.5 | -25.6% [-27.5, -23.9] | -10.7% | pass |
| preparation | 4 | combat100 | 1 | 842.6 | 905.8 | +7.5% [+7.0, +8.1] | +7.5% | FAIL |
| preparation | 4 | combat100 | 2 | 654.3 | 696.5 | +6.4% [+5.8, +7.1] | +8.1% | FAIL |
| preparation | 4 | combat100 | 4 | 458.7 | 486.8 | +6.1% [+5.4, +6.9] | +7.9% | FAIL |
| preparation | 4 | combat100 | 8 | 500.3 | 532.6 | +6.5% [+4.6, +8.8] | +5.1% | FAIL |
| preparation | 4 | ordinary4 | 1 | 432.4 | 435.7 | +0.8% [+0.1, +1.8] | +0.8% | pass |
| preparation | 4 | ordinary4 | 2 | 388.5 | 389.8 | +0.3% [-0.0, +0.7] | +0.6% | pass |
| preparation | 4 | ordinary4 | 4 | 176.9 | 178.7 | +1.0% [+0.1, +1.9] | +0.7% | pass |
| preparation | 4 | ordinary4 | 8 | 194.2 | 199.9 | +2.9% [+1.4, +4.4] | +2.4% | pass |
| preparation | 8 | empty100 | 1 | 558.7 | 483.1 | -13.5% [-14.4, -13.0] | -13.5% | pass |
| preparation | 8 | empty100 | 2 | 396.4 | 406.0 | +2.4% [+1.8, +3.3] | -10.2% | FAIL |
| preparation | 8 | empty100 | 4 | 269.9 | 205.7 | -23.8% [-25.7, -22.6] | -12.1% | pass |
| preparation | 8 | empty100 | 8 | 335.0 | 250.6 | -25.2% [-26.9, -23.4] | -11.1% | pass |
| preparation | 8 | combat100 | 1 | 839.1 | 900.6 | +7.3% [+6.8, +7.9] | +7.3% | FAIL |
| preparation | 8 | combat100 | 2 | 653.2 | 702.1 | +7.5% [+6.7, +8.5] | +9.1% | FAIL |
| preparation | 8 | combat100 | 4 | 459.0 | 490.4 | +6.8% [+5.4, +8.6] | +8.5% | FAIL |
| preparation | 8 | combat100 | 8 | 504.8 | 525.8 | +4.2% [+3.2, +5.2] | +4.1% | pass |
| preparation | 8 | ordinary4 | 1 | 429.2 | 429.6 | +0.1% [-0.2, +0.4] | +0.1% | pass |
| preparation | 8 | ordinary4 | 2 | 384.1 | 385.6 | +0.4% [-0.1, +1.0] | +0.9% | pass |
| preparation | 8 | ordinary4 | 4 | 177.4 | 179.5 | +1.2% [-0.9, +3.0] | +1.1% | pass |
| preparation | 8 | ordinary4 | 8 | 197.2 | 201.7 | +2.3% [+0.5, +4.0] | +2.8% | pass |

CPU breakdown at four participants (µs/tick): shared capture is charged only as its paired difference from original. This includes requirements inspection; common capture is not charged to targeting. Planning sums all executor threads. Publication excludes joins; wait is elapsed time, separate. Each probe has overhead.

| Variant | L | Workload | Original scans | Incremental capture | Source/facts | Index | Planning | Publication | Consumption | Admission overhead | Cached turret total |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| baseline | 4 | empty100 | 156.72 | 2.86 | 11.43 | 3.24 | 8.10 | 9.97 | 57.56 | 4.16 | 97.31 |
| baseline | 4 | combat100 | 9.57 | 0.63 | 19.79 | 6.19 | 54.84 | 14.55 | 3.70 | 4.98 | 104.68 |
| baseline | 4 | ordinary4 | 6.84 | 2.44 | 2.54 | 3.63 | 2.19 | 3.42 | 2.58 | 3.08 | 19.88 |
| baseline | 8 | empty100 | 158.53 | 3.14 | 12.22 | 3.29 | 8.13 | 10.04 | 59.57 | 4.14 | 100.54 |
| baseline | 8 | combat100 | 9.19 | 1.08 | 19.39 | 5.97 | 52.76 | 13.83 | 3.60 | 4.11 | 100.74 |
| baseline | 8 | ordinary4 | 6.94 | 2.33 | 2.53 | 3.61 | 2.17 | 3.36 | 2.56 | 3.01 | 19.56 |
| combined | 4 | empty100 | 154.21 | 1.22 | 8.19 | 2.87 | 6.74 | 7.66 | 58.14 | 3.53 | 88.35 |
| combined | 4 | combat100 | 9.09 | 2.35 | 4.03 | 2.90 | 5.27 | 3.45 | 3.65 | 1.78 | 23.43 |
| combined | 4 | ordinary4 | 7.10 | 1.76 | 1.99 | 3.28 | 1.92 | 3.19 | 2.67 | 2.71 | 17.51 |
| combined | 8 | empty100 | 156.14 | 2.13 | 8.07 | 2.88 | 7.76 | 7.49 | 57.91 | 3.60 | 89.84 |
| combined | 8 | combat100 | 9.19 | 1.41 | 4.09 | 2.90 | 5.12 | 3.33 | 3.61 | 1.80 | 22.27 |
| combined | 8 | ordinary4 | 7.04 | 1.11 | 1.99 | 3.17 | 1.85 | 3.05 | 2.61 | 2.64 | 16.44 |
| forecast | 4 | empty100 | 155.02 | 3.29 | 11.21 | 2.84 | 6.72 | 8.65 | 57.36 | 3.56 | 93.63 |
| forecast | 4 | combat100 | 9.24 | 1.63 | 5.45 | 2.90 | 5.06 | 3.44 | 3.57 | 1.76 | 23.80 |
| forecast | 4 | ordinary4 | 6.88 | 1.64 | 2.34 | 3.18 | 1.84 | 3.05 | 2.59 | 2.59 | 17.23 |
| forecast | 8 | empty100 | 156.85 | 2.82 | 10.95 | 2.90 | 6.83 | 9.02 | 58.78 | 3.83 | 95.12 |
| forecast | 8 | combat100 | 9.14 | 1.16 | 4.73 | 2.95 | 5.13 | 3.48 | 3.61 | 1.78 | 22.85 |
| forecast | 8 | ordinary4 | 7.04 | 2.71 | 2.43 | 3.26 | 1.86 | 3.14 | 2.67 | 2.70 | 18.77 |
| preparation | 4 | empty100 | 160.04 | 1.40 | 8.85 | 3.29 | 8.30 | 8.87 | 57.16 | 3.97 | 91.83 |
| preparation | 4 | combat100 | 9.10 | -0.70 | 11.82 | 6.04 | 52.77 | 11.84 | 3.61 | 3.92 | 89.31 |
| preparation | 4 | ordinary4 | 7.11 | 0.51 | 2.11 | 3.56 | 2.19 | 3.42 | 2.54 | 2.93 | 17.26 |
| preparation | 8 | empty100 | 159.52 | 2.56 | 8.52 | 3.41 | 8.32 | 8.87 | 57.99 | 4.07 | 93.75 |
| preparation | 8 | combat100 | 9.16 | 0.20 | 11.34 | 6.03 | 52.47 | 11.96 | 3.62 | 3.89 | 89.52 |
| preparation | 8 | ordinary4 | 7.16 | 0.88 | 2.12 | 3.60 | 2.27 | 3.51 | 2.61 | 3.03 | 18.02 |
