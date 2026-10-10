The final staged diagnostic wave used one retained early 512 map, 1,024 warmup and 2,048 measured ticks, eight compute slots and nine reserved physical cores. Every configuration used one discarded pair and two retained pairs with balanced CPU/GPU order. These are geometric paired scenario ratios, not the separately defined final arithmetic aggregate. Every run and failure is retained; no broad confirmation or original qualification is claimed.

| Plan | Check interval | CPU ratio | Wall ratio | Tick p99 ratio |
| --- | ---: | ---: | ---: | ---: |
| frozen8 | 1 | 0.7942 | 1.0396 | 1.1608 |
| frozen8 | 8 | 0.7809 | 1.0239 | 1.0882 |
| frozen8 | 32 | 0.7711 | 1.0498 | 1.2567 |
| frozen16 | 1 | 0.7879 | 1.0625 | 1.1535 |
| frozen16 | 8 | 0.7671 | 1.0309 | 1.0592 |
| frozen16 | 32 | 0.7674 | 1.0436 | 1.3307 |
| jacobi4 | 1 | 0.8530 | 1.1355 | 1.1015 |
| jacobi4 | 8 | 0.8038 | 1.0803 | 1.1625 |
| jacobi4 | 32 | 0.8053 | 1.0662 | 1.0714 |

Frozen16 at check interval 8 did not reduce dispatches relative to Frozen8: both used 25,808 dispatches and 3,226 checks for 942 observed committed fields. Interval 32 reduced checks but increased dispatches to 36,544. Jacobi4 had greater backend caller CPU and is not a retained finalist. The current source does not establish a better GPU default than the earlier Frozen8/check8 epoch development candidate.

The NVIDIA rendered preflight established ten distinct owned roles, but terrain CPU and presentation cadence differed between variants. Its apparent CPU reduction is not attributed to offloading. The default llvmpipe pair had CPU ratio 0.996. Actual warm process endpoints and complete exact traces are preserved separately from whole-session distributions.

The CPU reservation was removed after the last sample. No experiment or build was launched after the stop request. No binaries are archived; exact SHA-256 and source/build/dependency receipts are included. Archives are split by bounded 1024 fixture to stay within hosting blob limits. Extract archives into a dedicated evidence directory; original absolute paths are provenance and must be explicitly relocated for reproduction.

Bulky bounded 1024 checksum sidecars remain locally retained. The compact per-fixture archives include all result/configuration/progress receipts and own saves. `archive-members.json` marks local-only sidecars with original byte lengths and uncompressed hashes; `locally-retained-traces.json` gives full local archive hashes and paths. They are not claimed to be uploaded.
