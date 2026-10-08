from pathlib import Path
import json
r=Path('artifacts/resource-growth');b=r/'remaining';old=json.load(open('docs/.work/resource-growth-pr-before.json'))['body'];commit=(r/'evidence-commit.txt').read_text().strip();url=f'https://github.com/Globulation2/glob2/blob/{commit}/remaining';summary=json.load(open(b/'final-integration-comparison/summary.json'))
intro=old.split('## Snapshot copy optimization')[0]
start=intro.index('Timing, random sequencing')
intro=intro[:start]+'''Timing, random sequencing, within-batch feedback, stale-condition handling and unit-stock seeding deliberately change gameplay. The integration with current master uses save format/replay floor 147, protocol 65 and SIM 32; the save compatibility floor remains58. Master independently allocated formats144/145 to map/building artwork, overlapping earlier growth prototypes. Bounded layout probes preserve both144/145 lineages,146 growth saves and pending deadlines; seven real fixtures cover load/resave and owner/shared continuation. Golden traces and the match record are refreshed for the integrated gates.\n\n'''
body=intro+'''## Remaining optimization experiment

Implemented A (incremental totals), B (combined publication mutation), C (256-record stock-block tracking), D (coalesced sparse spatial copies), and independent/combined experimental builds. **None are adopted.** The existing contiguous snapshot-copy optimization remains.

The corrected isolated campaign used one warm-up plus ten paired runs per case, extending uncertain comparisons to 30. Individual candidates and A+B did not satisfy the full acceptance rule. All-four initially qualified against edb09d402, but fresh confirmation after master integration reversed the dense result: throughput−2.95% [−4.08,−1.50], CPU reduction−3.52% [−4.84,−1.40]. Saturated throughput fell2.99%; AI tick-p99 rose15.95%. Prototypes and raw evidence are retained outside the implementation branch. No selected candidate remained for the conditional delay/thread performance sweep; the correctness sweep did run.

Component improvements did not justify adoption: incremental stock updates, dirty stock blocks and coalesced rows substantially reduced their targeted costs, but whole-engine outcomes depended on workload and integration. The initial pilot had an all-off scaffolding/layout effect and is explicitly excluded from conclusions.

## Final comparison against latest master

Delivered commit `b7a47812222f34a1a4bccc97adc59f516769fa56` versus untouched master `67fd5b935c98c3d6d0c5ba9d25754ce7df0f83f9`. Four reserved physical cores and their SMT siblings, performance governors, identical starting saves/seeds/orders,1,024 ticks, delay 8/shared 4, one warm-up and ten alternating pairs. All builds/tests finished before timing. Positive values favor this branch; paired bootstrap 95% intervals are shown.

| Scenario | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved /1,024 ticks |
|---|---:|---:|---:|
'''
def pct(d):return f"{100*d['median']:+.2f}% [{100*d['ci95'][0]:+.2f}, {100*d['ci95'][1]:+.2f}]"
for s,d in summary['results']['retained-vs-latest-master'].items():body+=f"| {s} | {pct(d['tps_gain'])} | {pct(d['cpu_reduction'])} | {d['saved_ms']['median']:+.2f} ms |\n"
body+='''
This compares different growth behavior, not identical work. Final material stock is7.96% lower in multi-material,19.92% lower in sparse and20.29% lower in fragmented; disabled growth matches. Per-material totals, deposits, statistics and playable saves accompany timing. The matching-behavior comparison against edb09d402 is also in the full tables.

On the rejected integrated candidate, shared versus direct-owner calculation gave multi-material+9.49% throughput [5.89,11.12] with2.36% higher CPU; dense throughput fell1.67% with3.24% higher CPU. Against optimized-original with applicable common map/copy improvements, multi-material throughput+3.19% [−0.49,12.14] remained inconclusive, with3.86% higher CPU. Those controls use confirmation master 0f1a2569, not the final retained binary. No universal parallel speedup is claimed.

## Verification and limits

- Integrated current master 67fd5b935; no experimental switches or A–D code ship.
- Retained implementation:238 engine cases pass,11 display skips ; 29 unit cases pass; 12 golden cases pass. Version check passes against current master. The150 seeded-resource golden digests are unchanged; checksum differences are entirely the save-header contribution. An initially stale replay-floor constant was caught, corrected and fully retested; the failed run remains in evidence.
- Retained144-record differential matrix matches edb09d402 per tick across eight fixtures, delays 1/3/8, owner execution and shared 1/2/4/8. Corrected experimental builds also passed focused/differential testing before timing.
- Seven real fixtures preserve both144/145 save lineages and146 pending growth through current resave/continuation. Save floor 58 remains unchanged.
- Linux x86-64/Threadripper 2950X/GCC 15.2 release/O3 only. Windows/macOS/ARM/browser/threadless execution remains unavailable; display cases skipped. PR remains draft pending platform coverage and maintainer playtesting.
- Overlapping stage timers are not additive costs. Core reservations do not isolate shared memory/cache/package power. Snapshot allocation counters are not global heap counters; master tick histograms are not exact p99. All owned core reservations/governors restored.

## Evidence

'''
body+=f'[Results and adoption decision]({url}/README.md) · [Full isolated/combined/final tables]({url}/performance-tables.md) · [Verification and limits]({url}/verification-summary.md) · [Exact source/prototype reproduction]({url}/reproduction/README.md) · [Frozen identities]({url}/final-integration-freeze.json) · [Raw final timings and commands]({url}/final-integration-comparison) · [Reservation audit]({url}/cpuset-final-integration.json) · [Final resource outcomes and playable saves]({url}/final-work-comparison.md)\n\n'
body+='[Previous contiguous-copy results](https://github.com/Globulation2/glob2/blob/d986cec8c71fdcf294bed04ae16a089d3ef6f590/copy-policy/README.md) · [Earlier fair original/serial comparisons](https://github.com/Globulation2/glob2/blob/945577a18f5607c5f5ddc547c4b3df101954c6de/attribution-v2/README.md) · [Earlier ecology and compatibility evidence](https://github.com/Globulation2/glob2/blob/6f7dcad3af30a3ab4d10a7f8dfe4a4dbae6c6003/simple/README.md)\n'
Path('docs/.work/resource-growth-pr-final.md').write_text(body)
comment=f'''Tested and pushed `b7a47812222f34a1a4bccc97adc59f516769fa56`, integrated base `67fd5b935c98c3d6d0c5ba9d25754ce7df0f83f9`. No A–D optimization was adopted: fresh integrated confirmation failed acceptance. The final retained implementation was separately compared against edb09d402 and current master using ten reserved-core pairs across all eight fixtures.

Linux x86-64, Threadripper 2950X, GCC 15.2/O3; dependency/executable/source/fixture hashes and release commands are frozen.238 engine cases passed (11 display skips),29 unit cases passed,12 goldens passed. The per-tick 144-record retained matrix matches edb09d402 across delays 1/3/8 and owner/shared 1/2/4/8. Both144/145 lineages and146 pending saves load/resave/continue. Format147/protocol65/SIM32 resolves master's independent artwork-format allocations; the save floor remains58.

[Verification commands/logs/traces and omissions]({url}/verification-summary.md), [build identities]({url}/final-integration-freeze.json), [all performance tables with confidence intervals]({url}/performance-tables.md), [raw final paired runs]({url}/final-integration-comparison), [core reservation/restoration]({url}/cpuset-final-integration.json), [governor restoration]({url}/governor-final-integration.json), [resource work and playable saves]({url}/final-work-comparison.md), [reproducible experimental sources/scripts]({url}/reproduction/README.md).

Non-Linux, threadless and display execution were unavailable. These Linux checks do not establish cross-platform determinism. No hosted expensive check is claimed. The conditional performance sensitivity sweep was not run because no candidate qualified; correctness settings coverage was completed. Master uses different growth semantics, with quantified stock differences, so its timing gains cannot be called pure parallelism gains. PR stays draft.
'''
Path('docs/.work/resource-growth-evidence-comment.md').write_text(comment)
