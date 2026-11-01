# Director subagent's independent cohort review

Keep `prestige.enabled=false` by default. The corrected feature is reasonable as an opt-in capability, but the complete cohort does not establish positive effectiveness, and four pipelines were never selected. This is an effectiveness recommendation, not a new implementation defect finding.

This is the independent director review, distinct from the quant review and main assessment. Its initial files accidentally shared names with the quant agent and were overwritten. The original director aggregate stdout survived at `/tmp/prestige-independent-output.txt`; `director_review.py` reproduces this review directly from raw Results and verified stdout under unique filenames. It does not import or consume either other analyzer's aggregates. The report below preserves the director's original findings and20,000-draw seed51891 bootstrap, not the quant agent's bootstrap.

## Coverage and method

All288 successful games, all96 original map/rotation triples, and every arm were retained. Formats have32 duel triples and64 FFA triples. All arms used bundle `8c55351224b2eb50fd5995ee441b6b27d5f189441bf90a914f5ac906cdb58a89`, source archive SHA256 `8100dd19b42bdbf781ca1507b21e7e9717b1af3b700840dee6dbddd283b7d842`, SIM117/save193/net75. Historical outcomes are not controls.

The independent calculation uses integer `area_twice_permille_ticks / (2000 * observed_ticks)`, rather than rounded recorded means. All960 full team histories were independently checked against their integer trapezoid integrals. Paired inputs, seeds, limits, outputs, and all nonpolicy configuration were verified equal. Every checksum-verified stdout artifact was streamed to reconstruct capacity formulas, request bounds, cancellation observations, and subject economic sample-and-hold means. Economic coverage spans the subject's entire active lifetime, ending at elimination or game end. Confidence intervals use20,000 bootstrap draws with seed51891 and16 seed blocks per format, keeping all seat rotations together. Reproduction writes `director-review.json`, with all288 subject summaries and capacity-input examples.

## Effectiveness

Values are percentage points of time-normalized integrated relative model score. This softmax score is not an independent marginal win probability when teams can co-win.

| Format | Contrast | Mean change, pp | 95% seed-block interval, pp |
| --- | --- | ---: | --- |
| Duel | adaptive − baseline | +0.723 | [−2.918, +3.887] |
| Duel | single − baseline | +0.723 | [−2.918, +3.887] |
| Duel | adaptive − single | 0.000 | [0.000, 0.000] |
| FFA | adaptive − baseline | +1.087 | [−0.162, +2.437] |
| FFA | single − baseline | +1.114 | [−0.135, +2.453] |
| FFA | adaptive − single | −0.027 | [−0.100, +0.019] |

| Format/arm | Engine wins/losses/unresolved | Subject natural prestige wins, sole/shared |
| --- | --- | --- |
| Duel/baseline | 13/13/6 | 1/0 |
| Duel/single | 15/10/7 | 1/2 |
| Duel/adaptive | 15/10/7 | 1/2 |
| FFA/baseline | 10/29/25 | 0/4 |
| FFA/single | 9/33/22 | 3/2 |
| FFA/adaptive | 10/32/22 | 4/2 |

Natural prestige finishes are inferred from an engine end with multiple teams alive under frozen stock rules. These are actual engine victories, including shared victories; probability adjudication of caps does not create independent actual victories. Duel capped games number6/7/7, and FFA caps35/32/32, respectively. FFA caps can include an already eliminated subject, so cap counts differ from unresolved counts.

Adaptive and single have identical duel integrated scores and outcomes, but one duel checksum differs. Only three of96 final checksums differ across these two arms:

- `1v1:islands:1012:1`: both unresolved at90000 with score0.787416 and prestige300. One late capacity2 observation changes internal state without a measured outcome effect.
- `ffa:islands:1010:0`: both sole prestige wins with300 points. Adaptive finishes at84866 instead of89314, yet its integrated model score is lower,0.425731 versus0.447047. Faster actual victory and a lower duration-normalized score can coincide; this is not automatically a strategic regression.
- `ffa:islands:1012:0`: adaptive wins alone at58722 with200 prestige; single loses to team2 at57410 with150. Scores0.189502 versus0.185440. This is a concrete positive example, not a population-effect estimate from selecting activated games.

## Capacity and observed safety

Only12 of13929 adaptive capacity observations exceed1: capacity2 occurs nine times and capacity3 three times, across the three checksum-different games above. Capacity4 is never selected;13917 observations select1. Requests do not prove simultaneous funded crews or additional completed providers.

Reconstructed examples:

- FFA islands1010seat0 tick68775: idle94 + root assigned0 − reserve30 − ordinary unmet15 − unobserved crews0 =49 spare. Crew12 gives worker limit4, material limit4, but three remaining steps cap the choice at3. Desired7 includes four finished providers.
- FFA islands1012seat0 tick48969: idle51 + root assigned1 − reserve15 − ordinary unmet11 =26 spare. Crew12 permits2; four remaining steps leave choice2.
- Duel islands1012seat1 tick88032: idle78 − reserve35 =43 spare. Worker limit3 is capped by two remaining steps, giving2.

The independent telemetry audit found no capacity-formula, configured-limit, Finish, remaining-step, active/inactive-request-bound, or disabled-policy-event violations. Eligibility and material-debit truth are not independently reconstructed per unit from these logs; they depend on code and focused tests. Four-pipeline execution, custom catalogs, and scarce communal-only materials have no direct whole-match support here.

Each enabled arm issued one parallel-commitment cancellation in FFA islands1010seat2; each resolved. No old unresolved cancellation or terminal losing-team own-prestige-gain flag was found. These observations do not prove universal completion safety: telemetry samples omit complete generation/receipt/inter-tick ordering. The NEW/REPAIR and unsafe-revert fixes rely on focused regressions rather than activation in these stock games.

## Economic pressure and adverse examples

Equal-game averages of time-weighted director observations are descriptive. Adaptive minus baseline in duels: population−4.718, trained workers−2.616, schools+0.032, unserved food−1.245, critical food−1.265. FFA: population+2.569, trained workers−0.890, schools+0.120, unserved food+0.688, critical food+0.622, food security−1.135. Lifetime/end-time and military changes can produce these differences; they do not prove school spending caused hunger or reduced growth. Adaptive-single differences are small because most trajectories coincide.

| Pair | Adaptive − baseline score, pp | Baseline → adaptive outcome | Maximum selected capacity |
| --- | ---: | --- | ---: |
| Duel islands1010seat1 | −22.059 | win → unresolved | 1 |
| Duel continents1007seat1 | −19.509 | win → unresolved | 1 |
| FFA continents1006seat3 | −28.506 | win → unresolved | 1 |
| FFA symmetric-arena1001seat0 | −16.964 | unresolved → unresolved | 1 |
| FFA islands1009seat1 | −16.029 | unresolved → loss | 1 |

These adverse trajectories match single exactly, so they do not identify harm from pipelines2–4. Duel islands1010seat1 has lower average population281.1 versus357.3, but also lower food-pressure counts; starvation is not an established cause. Continents1007seat1 has more schools1.147 versus0.405 and trained workers11.94 versus7.26, yet lower population51.57 versus62.00 and a worse outcome. Economy/military tradeoffs merit investigation, without declaring a proven mechanism. The previously adverse duel symmetric-arena1004seat0 is now positive: enabled policy wins at77090 versus a baseline cap, with score+10.560pp.

## Recommendation and limits

Keep the feature opt-in and preserve older-save capacity1 migration. Describe configured max4 as a tested guarded capability, not an empirically superior default. Actual prestige victories demonstrate that the route functions, while overall outcomes are mixed and all primary mean-benefit intervals include zero.

Coverage is the frozen stock catalog, three map families,16 seeds, all original rotations, off-policy Maxima opponents,90000-tick caps, and zero probability early stopping. Other opponents, broader maps, custom positive-prestige layouts, disabled upgrades, and empirical capacity4 execution are outside this cohort.16 independent blocks give limited precision. Activation-subset comparisons are not causal estimates; model adjudication is not independent corroboration of the model integral. No production files were changed during this independent review or its filename recovery.
