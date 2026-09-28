# Baseline verification

128 matched starts, 16 map families, four seeds and two rotations. Four archival builds add 512 games to the completed 256 current/master games. Common-window food measurements use the last shared logged interval across all six versions. Full-run geometric harvest is also shown for comparability with earlier studies. Intervals bootstrap whole map families (5000 draws), exploratory 95%. Retained maps, not an independent holdout.

| Comparison | Common-window wheat | 95% interval | Common-window meals | Full-run geometric wheat |
|---|---:|---:|---:|---:|
| permanent vs week-ago | +4.2% | -1.4% to +10.2% | +5.6% | +8.6% |
| permanent vs pre-farming | +4.2% | -2.8% to +11.7% | +4.5% | +5.1% |
| permanent vs experiment-baseline | +14.4% | +7.2% to +23.9% | +12.6% | +13.7% |
| permanent vs experiment-boundary | +5.2% | +0.7% to +11.8% | +4.3% | +4.7% |
| permanent vs master | -14.2% | -21.0% to -7.8% | -8.5% | -15.3% |
| experiment-boundary vs experiment-baseline | +8.7% | +3.8% to +14.1% | +8.0% | +8.6% |

## Fixed Nicowar opponent (64 starts)

| Comparison | Common-window wheat | 95% interval | Meals | Full-run geometric wheat |
|---|---:|---:|---:|---:|
| permanent vs week-ago | +9.7% | +3.2% to +17.1% | +11.4% | +19.8% |
| permanent vs pre-farming | +8.9% | +3.7% to +14.6% | +8.9% | +11.6% |
| permanent vs experiment-baseline | +14.5% | +7.7% to +22.2% | +8.4% | +21.7% |
| permanent vs experiment-boundary | +1.1% | -0.5% to +3.8% | +0.1% | +3.4% |
| permanent vs master | -16.1% | -26.6% to -6.2% | -10.1% | -9.4% |
| experiment-boundary vs experiment-baseline | +13.3% | +7.4% to +20.0% | +8.3% | +17.6% |

Half the starts face Nicowar; half face Maxima. Production builds change both Maxima players, while archived experiments change only team 0. Therefore the Nicowar subset is the clean fixed-opponent policy comparison. The all-start table measures complete-version outcomes, including changed opposing behavior in self-play. The opponent groups also use different map seeds, so differences between groups cannot be attributed solely to the opponent. Earlier METHOD wording incorrectly called all opponents Nicowar; cases.json and actual commands preserve the correct assignments.

## Quiet crowded-save CPU

| Version | Mean CPU, 8192 ticks | Permanent vs version |
|---|---:|---:|
| week-ago | 37.065s | +16.8% |
| pre-farming | 36.617s | +18.2% |
| master | 46.940s | -7.8% |
| permanent | 43.280s | +0.0% |

Same original crowded save, four repetitions per version, pinned physical cores on therig after games and metric parsing finished. Experimental binaries are excluded from CPU comparisons because they include extra diagnostic instrumentation. CPU differences include changed simulation trajectories/populations.

The original experiment baseline already includes expansion support and wood reserves. Pre-farming master predates those changes. Week-old master additionally predates unrelated work. The earlier positive spacing result and a loss versus maturity can coexist; their denominators differ.

The fresh runs last at most 32,768 ticks, and do not establish indefinite sustainability. See summary.json for full intervals, starvation rates, family effects and totals.
