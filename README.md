# Compact unit core evidence

[New reserved-core live match comparison](live-report.md) · [Live raw evidence](live-evidence.zip)

Ten paired release repetitions per scenario, with concurrent simulation and software presentation: the normal 1,024-unit colony uses about **1.2% less game CPU** (95% CI 0.75–1.76%). Capture is about **35% cheaper**, with **36% less logical capture payload**. Both variants hold 60 FPS/30 TPS; uncapped FPS and maximum TPS gains are inconclusive. The full-observation Cortex case and ordinary control are reported separately. CPU restoration audits, all paired samples and byte-identical live replay/checksum evidence are included. Linux software/Xvfb coverage does not establish GPU/desktop-display performance.

![Live benefit confidence intervals](live-benefit.png)


[New capture-inclusive runtime follow-up](runtime-report.md) · [Runtime raw evidence](runtime-evidence.zip)

Twenty paired GUI repetitions and a separate longer-window replication establish a large-colony capture-inclusive pipeline benefit. The report preserves ordinary/panel limitations and distinguishes this from live-match frame rates.

![Capture attribution and measured pipeline costs](capture-runtime-phases.png)


[Report and limitations](report.md) · [Raw evidence archive](compact-core-evidence.zip) · [Source/platform metadata](source-state.json)

![Record, capture and Callgrind comparisons](compact-core-evidence.png)

![Engine and drawing confidence intervals](compact-runtime-intervals.png)

The timings compare frozen revisions; final integration checks are separately identified in the report. Movement remains Stage A. The archive includes raw commands, samples, profiles, JUnit XML, baseline reproductions, saves/replays/checksums and corrected software images. Reproduced baseline failures and unavailable platform/gameplay coverage remain explicit.


[Real-game direction agreement and delay sweep](agreement-report.md) · [Raw metrics/traces/commands](agreement-evidence.zip)

Six active AI games, diagnostic delays 0 through 8, 54 matched reference comparisons. Exact agreement is 38.99% to 38.77%; usable coverage is 56.45% to 55.81%. Resource steering agrees about 75%, while building comparison coverage is poor and requires validation-semantic investigation. These are conditional steps along unchanged reference histories, not adopted-policy gameplay. The report links deduplicated initial saves, replays and checksums for every game. The core/detail layout remains unchanged.

![Direction agreement and comparison coverage versus delay](agreement-delay.png)
