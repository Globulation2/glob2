# Classic gradient equivalent-work analysis

**EXPLORATORY ONLY: runtime host bounds failed. These timings are not accepted performance evidence.**

Validated 217,728 rows across 9,072 combinations: 756 cases, three readers, two allocation layouts and two link orders. Each has eleven alternating warm pairs plus two workspace-cold solves.

Every full field passed the frozen driver’s unconditional independent oracle comparison before its row was emitted. Raw rows did not record an explicit oracle-pass flag; the verified driver hash and completed matrix establish this contract.

Ratios below 1 indicate lower final CPU time. Each aggregate is sum(final case median CPU) / sum(legacy case median CPU). The four layout strata remain separate; there is no pooled headline speedup.

| Reader | Workload group | Buffers | Link order | Cases | Final / legacy CPU | Change |
| --- | --- | --- | --- | ---: | ---: | ---: |
| eager | active_full_field_128 | separate | final-first | 28 | 0.9117 | -8.83% |
| eager | active_full_field_256 | separate | final-first | 28 | 0.8757 | -12.43% |
| eager | active_full_field_512 | separate | final-first | 28 | 0.3637 | -63.63% |
| eager | active_full_field_large | separate | final-first | 84 | 0.4193 | -58.07% |
| eager | full_matrix | separate | final-first | 756 | 0.4463 | -55.37% |
| eager | active_full_field_128 | separate | legacy-first | 28 | 0.9046 | -9.54% |
| eager | active_full_field_256 | separate | legacy-first | 28 | 0.8638 | -13.62% |
| eager | active_full_field_512 | separate | legacy-first | 28 | 0.3629 | -63.71% |
| eager | active_full_field_large | separate | legacy-first | 84 | 0.4191 | -58.09% |
| eager | full_matrix | separate | legacy-first | 756 | 0.4460 | -55.40% |
| eager | active_full_field_128 | shared | final-first | 28 | 0.8926 | -10.74% |
| eager | active_full_field_256 | shared | final-first | 28 | 0.8589 | -14.11% |
| eager | active_full_field_512 | shared | final-first | 28 | 0.3578 | -64.22% |
| eager | active_full_field_large | shared | final-first | 84 | 0.4124 | -58.76% |
| eager | full_matrix | shared | final-first | 756 | 0.4408 | -55.92% |
| eager | active_full_field_128 | shared | legacy-first | 28 | 0.9077 | -9.23% |
| eager | active_full_field_256 | shared | legacy-first | 28 | 0.8571 | -14.29% |
| eager | active_full_field_512 | shared | legacy-first | 28 | 0.3598 | -64.02% |
| eager | active_full_field_large | shared | legacy-first | 84 | 0.4153 | -58.47% |
| eager | full_matrix | shared | legacy-first | 756 | 0.4419 | -55.81% |
| packed16-control | active_full_field_128 | separate | final-first | 28 | 0.9728 | -2.72% |
| packed16-control | active_full_field_256 | separate | final-first | 28 | 1.0067 | +0.67% |
| packed16-control | active_full_field_512 | separate | final-first | 28 | 0.9683 | -3.17% |
| packed16-control | active_full_field_large | separate | final-first | 84 | 0.9753 | -2.47% |
| packed16-control | full_matrix | separate | final-first | 756 | 0.9854 | -1.46% |
| packed16-control | active_full_field_128 | separate | legacy-first | 28 | 1.0083 | +0.83% |
| packed16-control | active_full_field_256 | separate | legacy-first | 28 | 1.0038 | +0.38% |
| packed16-control | active_full_field_512 | separate | legacy-first | 28 | 1.0348 | +3.48% |
| packed16-control | active_full_field_large | separate | legacy-first | 84 | 1.0278 | +2.78% |
| packed16-control | full_matrix | separate | legacy-first | 756 | 1.0130 | +1.30% |
| packed16-control | active_full_field_128 | shared | final-first | 28 | 0.9786 | -2.14% |
| packed16-control | active_full_field_256 | shared | final-first | 28 | 0.9598 | -4.02% |
| packed16-control | active_full_field_512 | shared | final-first | 28 | 0.9507 | -4.93% |
| packed16-control | active_full_field_large | shared | final-first | 84 | 0.9536 | -4.64% |
| packed16-control | full_matrix | shared | final-first | 756 | 0.9681 | -3.19% |
| packed16-control | active_full_field_128 | shared | legacy-first | 28 | 0.9999 | -0.01% |
| packed16-control | active_full_field_256 | shared | legacy-first | 28 | 0.9972 | -0.28% |
| packed16-control | active_full_field_512 | shared | legacy-first | 28 | 0.9930 | -0.70% |
| packed16-control | active_full_field_large | shared | legacy-first | 84 | 0.9941 | -0.59% |
| packed16-control | full_matrix | shared | legacy-first | 756 | 1.0051 | +0.51% |
| snapshot | active_full_field_128 | separate | final-first | 28 | 0.9916 | -0.84% |
| snapshot | active_full_field_256 | separate | final-first | 28 | 1.0187 | +1.87% |
| snapshot | active_full_field_512 | separate | final-first | 28 | 1.0274 | +2.74% |
| snapshot | active_full_field_large | separate | final-first | 84 | 1.0242 | +2.42% |
| snapshot | full_matrix | separate | final-first | 756 | 1.0115 | +1.15% |
| snapshot | active_full_field_128 | separate | legacy-first | 28 | 1.0239 | +2.39% |
| snapshot | active_full_field_256 | separate | legacy-first | 28 | 1.0107 | +1.07% |
| snapshot | active_full_field_512 | separate | legacy-first | 28 | 1.0249 | +2.49% |
| snapshot | active_full_field_large | separate | legacy-first | 84 | 1.0222 | +2.22% |
| snapshot | full_matrix | separate | legacy-first | 756 | 1.0346 | +3.46% |
| snapshot | active_full_field_128 | shared | final-first | 28 | 0.9677 | -3.23% |
| snapshot | active_full_field_256 | shared | final-first | 28 | 1.0055 | +0.55% |
| snapshot | active_full_field_512 | shared | final-first | 28 | 0.9906 | -0.94% |
| snapshot | active_full_field_large | shared | final-first | 84 | 0.9922 | -0.78% |
| snapshot | full_matrix | shared | final-first | 756 | 1.0022 | +0.22% |
| snapshot | active_full_field_128 | shared | legacy-first | 28 | 1.0176 | +1.76% |
| snapshot | active_full_field_256 | shared | legacy-first | 28 | 1.0265 | +2.65% |
| snapshot | active_full_field_512 | shared | legacy-first | 28 | 1.0291 | +2.91% |
| snapshot | active_full_field_large | shared | legacy-first | 84 | 1.0281 | +2.81% |
| snapshot | full_matrix | shared | legacy-first | 756 | 1.0228 | +2.28% |

Host observation: 223 samples; maximum load 9.02, compiler processes 2, sibling busy 67.29%; 12 sibling windows exceeded the recorded bound.

The JSON report retains all partitions, every paired ratio, all layout sensitivity comparisons, source/binary evidence hashes and the host protocol outcome. Paired intervals are suppressed when host bounds fail. Otherwise they are reported only for matched active square full-field cases at 128²–512². No aggregate interval is invented by treating unrelated workloads or code layouts as interchangeable samples.

Active large fields use full propagation, exclude both obstacle-separated and entirely blocked controls, and include single/sparse/dense/deferred seeds. Capped, rectangular/thin, blocked and 32² fixtures are separately identified. Swimming classes 0 and 3 avoid terrain reads and are additionally separated in JSON.

Queue addresses remain private and unmatched. Input/output addresses match only in shared-buffer mode. Snapshot creation and caller buffer copies are excluded from propagation CPU, and their separate preparation timings are illustrative one-shot metadata. These are classic equivalent-work fields, not new-terrain comparisons, and they imply no whole-game speedup.
