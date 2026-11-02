# All-case joint-three-arm food telemetry

Designed scenarios:1152; successfulthree-arm gamesets:1144; jointlyobservedfoodhistories:1144.

All frozen designed three-arm scenarios enter the expected inventory; no hunger,changed-case,outcome or anecdote selection. Missing measurements unavailable, never zero.
Each scenario uses latest exact published timestamp shared by all three arms, at/before their shortest game. The two allies are additionally intersected for2v2 alliance totals. No forwardfill/interpolation of stocks/cumulativecounters.
All three contrasts use identical joint observation window within each scope/scenario. Counter durations vary acrossscenarios due earlystopping; rates per10k observed ticks and worker deaths/Mexactworker-ticks are secondary, not full-horizon effects.
Exact fixed checkpoints16384/32768/49152/65536/81920 retain joint-observed coverage; changing risksets and earlystops prevent unconditional late causalinterpretation. No fabricated telemetry outcome tails.
Missing counter-family fields stay unavailable, not zero; allied sums require each component to exist for both allies. Whole-match adjustedscore is not repeated as a fixed-checkpoint score.
Point estimates:scenario means withinformat/generator-seed, seeds within generator,generators equally; formats equally inaggregate. All available jointcases remain, including incompletegeometricblocks; percell coverage isexplicit.
Generator weights are equal among available generators for each planned format; missing generator/format cells remain unavailable. Aggregate requires every planned format, without silently reweighting missing formats.
Hierarchicalpercentilebootstrap resamples generator thenmapseed, retainingallformats,rosters/rotations andpairedarms jointly. Missingformatcells remainunavailable. Medians/quantiles describepairedobserved distribution.
Fillingworkerticks describeactualservicing activity, not requested/assigned staffing. Denominator=sum11labouractivities+16fillingphases. Vanilla WORKER0 death/exposure rate isexplicit.
2v2allied0+1rawcounters summedbefore ratios. Internalnetconversions cancel; subjectpopulation/food isnot alliance survival. Adjustedscore remains official log_time_outcome_v1,c2,absoluteconfiguredhorizon; scorealwaysalliance-level on2v2.
Saved/partial counter coverage notstartingat0 isunavailable for0-to-commonend rates; missingfields neverzero.
950 and0stopping cohorts,horizons andbuildidentities separated; no pooling. Primaryeffectiveness remains the frozenmainanalyzer; this is separatelyprovenanced secondarytelemetry.
Manytelemetrymetrics have unadjusted intervals; no multiplicity-corrected confirmatorymechanismclaims.

|Stopping cohort|Scope|Contrast|Pairs|Metric|Balanced delta|Hierarchical95% interval|Median|
|---|---|---|---:|---|---:|---|---:|
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | adjusted_score | 0.2999 | [-0.0911, 0.7254] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | population | 1.6629 | [-0.3187, 4.0686] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | births | -0.0158 | [-0.2488, 0.2204] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | starvation | -0.0286 | [-0.1755, 0.0932] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | meals | 0.6701 | [0.1360, 1.3780] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | delivered | 0.8468 | [-0.7751, 2.6655] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | worker_starvation_rate | -0.0116 | [-0.1229, 0.0791] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | inn_workers | 0.0804 | [-0.0431, 0.2225] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | swarm_workers | -0.1685 | [-0.3492, -0.0414] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | inn_share | 0.0735 | [-0.0407, 0.2368] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | swarm_share | -0.1848 | [-0.2911, -0.0905] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus baseline | 1144 | eat_no_inn_share | -0.0455 | [-0.1322, 0.0199] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | adjusted_score | 0.3473 | [-0.2590, 1.0159] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | population | -1.2995 | [-5.1135, 2.4338] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | births | -1.0469 | [-1.6469, -0.4674] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | starvation | -0.1522 | [-0.3959, 0.0787] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | meals | 0.7309 | [-0.8918, 2.2711] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | delivered | -3.8049 | [-8.2746, 0.2594] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | worker_starvation_rate | -0.1748 | [-0.4938, 0.0645] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | inn_workers | -0.1616 | [-0.5299, 0.1793] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | swarm_workers | -1.1905 | [-1.8787, -0.6387] | -0.0845 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | inn_share | 0.0460 | [-0.2551, 0.3827] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | swarm_share | -1.3523 | [-1.7765, -0.9659] | -0.3585 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm8 minus baseline | 1144 | eat_no_inn_share | -0.2715 | [-0.5760, -0.0481] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | adjusted_score | -0.0474 | [-0.7602, 0.6230] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | population | 2.9624 | [-0.8770, 6.8814] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | births | 1.0311 | [0.4425, 1.5927] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | starvation | 0.1236 | [-0.1001, 0.3516] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | meals | -0.0608 | [-1.5225, 1.4516] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | delivered | 4.6516 | [0.7394, 8.7712] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | worker_starvation_rate | 0.1631 | [-0.0769, 0.4972] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | inn_workers | 0.2420 | [-0.0491, 0.5598] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | swarm_workers | 1.0220 | [0.5657, 1.5564] | 0.0852 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | inn_share | 0.0275 | [-0.2380, 0.2848] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | swarm_share | 1.1674 | [0.8184, 1.5333] | 0.3064 |
| p950-h90000-secondary0-b7758e379d2bc | subject | inn8-swarm12 minus inn8-swarm8 | 1144 | eat_no_inn_share | 0.2260 | [0.0237, 0.4932] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | adjusted_score | 0.0809 | [-0.2635, 0.4596] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | population | 0.5130 | [-0.9819, 2.2370] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | births | -0.1436 | [-0.4273, 0.0350] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | starvation | -0.0696 | [-0.2077, 0.0448] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | meals | 0.0858 | [-0.4952, 0.6752] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | delivered | -0.4328 | [-2.2969, 0.7703] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | worker_starvation_rate | -0.0288 | [-0.1194, 0.0569] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | inn_workers | 0.0486 | [-0.0682, 0.2205] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | swarm_workers | -0.1613 | [-0.3626, -0.0172] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | inn_share | 0.0429 | [-0.0507, 0.1713] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | swarm_share | -0.1049 | [-0.2538, 0.0168] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus baseline | 384 | eat_no_inn_share | 0.0033 | [-0.0835, 0.0878] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | adjusted_score | 0.0093 | [-1.1205, 0.9987] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | population | -2.8151 | [-8.3393, 2.6980] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | births | -0.9386 | [-1.8885, -0.0136] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | starvation | -0.3937 | [-0.7576, -0.1160] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | meals | 0.1091 | [-1.2737, 1.5129] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | delivered | -4.2501 | [-9.9976, 1.6042] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | worker_starvation_rate | -0.1989 | [-0.5005, 0.1102] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | inn_workers | -0.0611 | [-0.3587, 0.2151] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | swarm_workers | -0.9290 | [-1.6786, -0.3361] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | inn_share | 0.1565 | [-0.1199, 0.4637] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | swarm_share | -0.8637 | [-1.3702, -0.4077] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm8 minus baseline | 384 | eat_no_inn_share | -0.3145 | [-0.5889, -0.0874] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | adjusted_score | 0.0715 | [-1.0368, 1.3267] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | population | 3.3281 | [-2.5557, 9.6120] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | births | 0.7950 | [-0.1164, 1.7182] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | starvation | 0.3241 | [0.0687, 0.6514] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | meals | -0.0233 | [-1.5104, 1.3603] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | delivered | 3.8173 | [-2.0029, 9.7655] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | worker_starvation_rate | 0.1701 | [-0.1218, 0.4593] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | inn_workers | 0.1097 | [-0.2283, 0.5018] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | swarm_workers | 0.7677 | [0.2966, 1.3264] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | inn_share | -0.1136 | [-0.4068, 0.1674] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | swarm_share | 0.7588 | [0.3707, 1.1988] | 0.0000 |
| p950-h90000-secondary0-b7758e379d2bc | alliance | inn8-swarm12 minus inn8-swarm8 | 384 | eat_no_inn_share | 0.3178 | [0.0917, 0.5898] | 0.0000 |
