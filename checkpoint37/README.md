# Capability-driven buildings: integration evidence

This is review evidence for **draft PR805, not a completion or performance-acceptance claim**. The implementation branch adds per-game capability catalogs, experimental definitions, engine composition, every native AI's provider selection, scripting APIs, existing catalog-driven interfaces and persistence/transport contracts. The maintained authoring guide is `docs/features/building-catalogs.md` on that branch.

Final integrated source: `7fe776834664300abf24270ed2ac16822885cf5c`, incorporating renderer master `cbf71a464969ceda7a786696f89ae66ea7cf52ff`. The completed performance comparison is separately and strictly scoped to candidate `63a3b14098323ef0b5f8657c1d78521f29cb7ade` versus master `01848dea7790884bd341c66754253e2b9845e85a`; it predates the cold HUD fix and renderer merge. The archive identifies every earlier checkpoint individually. Curation HEAD does not relabel old tests as final-source runs.

## Results and remaining gates

- Checkpoint 35: 188 native core passes, with three display cases skipped there and all three separately passing;279 broad AI cases pass. Native JavaScript corpus and committed-match verification pass. Eight games spanning all native AIs reproduce34→35 replay bytes and component checksums exactly after the optimization.
- Browser 35: 21 focused cases pass,15 native/browser traces match, and 24 additional maintained suite runs execute 438 inner cases. Chromium, Firefox and WebKit are covered in serial/threaded modes; the six custom-composition runs execute 73 cases each. Full 33-case browser and strict scripting-corpus coverage is separately retained at 27. Broad interface/rendering coverage is retained at 32.
- Checkpoint 36: two new HUD regressions pass; real row drawing, marker bounds, stock horizons, absent instant-service rows, downstream clicks and simulation-checksum invariance are covered. Synthetic valid visit clocks in an extracted Scene exercise presentation; these are not additional Unit lifecycle tests.
- Final integrated source37 passes the native JavaScript corpus and committed-match verification. Both retained native reference traces are unchanged from checkpoint35. All 38 focused native renderer/HUD cases pass with no skips; captures were inspected. Final browser37 HUD coverage passes in all six browser/mode combinations (12 maintained case executions). Default-entry torus coverage is 7/9: two Firefox sky-pixel assertions fail, although the retained frame shows a torus. An incorrectly addressed `/threaded/` cohort produced nine harness startup failures and establishes no rendering coverage; forced-serial torus remains unverified. The corrected-URL startup check reached MainMenu in all six cases; five mode assertions passed. WebKit requested-threaded WebGL fell back to serial with `worker shared memory unavailable`, so its mode assertion failed and is retained. No failed torus assertion was rerun or reclassified. These failures and test-runner defects remain in the evidence; they are not treated as passes.
- **Performance: 49 of 55 master/candidate endpoints pass, three fail and three are inconclusive.** Both unused-catalog scaling endpoints pass. See [the concise performance review](performance35-public-summary.md), the raw archive and independent audits. All eight whole-game CPU groups pass, but two small natural-resource kernels and dense Cortex decisions repeatably exceed 3%. They remain failures. The closed independent checkpoint34 maintenance follow-up has six passes and two inconclusive cases; its samples are not pooled with the original cohort.
- Human gameplay review remains open, especially Maxima's documented allocation/pacing change. Native Windows/macOS/Android/iOS execution and other native architectures are unverified. Browser execution does not establish those platforms. The known baseline 16-bit PNG ImageAssets failure remains disclosed.

Historical RNG call order is not preserved through compatibility scaffolding. Current deterministic execution, exact save/load continuation, resource accounting and compatibility rejection boundaries remain required. Exact post-optimization traces show unchanged execution in the tested scenarios, not universal equivalence or historical-master gameplay parity.

Maxima's independent-service allocation fixes a composition problem but changes economy pacing. Three 30,000-tick peaceful/no-upgrade controls have mean final population 124.3 versus 189 on master, with zero starvation deaths. In broader 48-game execution coverage, 12 Maxima samples average 128.3 versus 145.3. These small fixed-seed samples are not win-rate estimates or proof of unchanged strength. An earlier checkpoint28 (`d5ae10ca5` versus master `410ce0f`) 512²/16-Maxima memory workload used 1541.7 MiB versus 1383.8 MiB; both changed trajectories and the observed 11.4% gap are retained.

Browsable results: [native renderer/HUD](native37-summary.md), [browser35](browser35-summary.md), [final browser37 results and limits](browser37-summary.md), [mixed-service HUD](images/mixed-service-progress.png), [instant-service HUD](images/instant-services.png). Original captures and pixel-preserving conversion checks are also in the archive.

## Reproducing and inspecting evidence

Environment: Linux x86-64, GCC 13.4.0 release builds; browser builds use Emscripten 4.0.15. Per-run manifests retain exact compiler/linker flags, SDL/dependency paths and hashes, source revisions, executable hashes, seeds, commands and inventories. The fixed paired protocol used benchmark CPU 12, identical comparator/candidate toolchains and dependencies, and counterbalanced process order. Independent review verified 1,119 raw processes and 13,860 paired kernel digests. A separate worktree's Firefox load was active on CPUs 4–7, excluding the benchmark core, SMT sibling and L3 group; shared-package effects were not measured, so no causal attribution or sample exclusion is made.

The archive contains the complete selected logs, JSON/XML results, replay/save/trace fixtures, PNG/BMP captures, benchmark samples, assembly/profiles and independent reviews. Earlier failed cohorts and obsolete drafts are deliberately retained and labeled; the current summaries take precedence over historical pending statements. `source.json` is curation metadata; `files.json` records SHA256 and length of every selected artifact. Paths inside command manifests describe reproduction inputs, not files that reviewers must access on the author's machine.

The Zstandard archive is approximately 207 MiB and expands to about 10.1 GiB; extraction requires `zstd` and `tar`. Browsable summaries above are available without downloading it.

Download all `evidence.tar.zst.partNNN` files, `SHA256SUMS` and `archive.json` from this directory into one directory. Verify and reassemble:

```sh
sha256sum -c SHA256SUMS
cat evidence.tar.zst.part[0-9][0-9][0-9] > evidence.tar.zst
sha256sum evidence.tar.zst  # compare with archive.json
mkdir extracted
zstd -d --stdout evidence.tar.zst | tar -xf - -C extracted
```

The `building-capabilities-evidence/` directory contains the files identified by `files.json`. Duplicate content uses normal tar hard links. The public staging scan reports its bounded credential-pattern checks in `privacy-preflight.json`; only explicitly curated evidence is included.
