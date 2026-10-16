# Windows shader-receipt failure

The Windows job failed before compilation or runtime testing. Windows runtime
compatibility remains unverified. This evidence does not establish a green
Windows build or substitute for cross-platform checksum comparison.

- Feature revision: `f56cbc8f624dd35c2066cce1aad51d0d9ada25cf`.
- Integration base: `987c953698bb3f0bfd1078184773075ba3313091`.
- Hosted merge revision: `077a81087b3fbb8206c1ca7ed919cab95da4fc0b`.
- Run: https://github.com/Globulation2/glob2/actions/runs/38832889808
- Job: https://github.com/Globulation2/glob2/actions/runs/38832889808/job/116757123315
- Full downloaded job log: `windows-job.log`.

The log records a clean checkout with `core.autocrlf=true` at lines 745–772 and
fails at lines 996–1006 in `renderer_shaders.configure` / `gradient_compute.verify`:

```
ValueError: Stale gradient shader bundle: src/field/GradientComputeShaders.h; regenerate with tools/shaders/gradient_compute.py
```

The feature did not change the generator, header, shader inputs, Vulkan receipts,
Git attributes or relevant Windows workflow. All eleven files used in this
reproduction are byte-identical to the integration base; the per-path comparison
is retained in `checkout-reproduction.json`. The verifier hashes raw file bytes,
while the repository has no line-ending rules for receipt-covered files. Git
conversion therefore invalidates otherwise unchanged checked-in receipts.

## Reproduction

From the repository root:

```sh
python3 artifacts/delayed-gradients/windows-gradient-bundle/reproduce.py
```

The script uses a disposable index (`GIT_INDEX_FILE`), loads `HEAD` with
`git read-tree HEAD`, and materializes only the receipt inputs into disposable
directories. The feature checkout, normal index and refs remain unchanged.
For the same file list it executes these policies; the exact expanded command
arrays are in `checkout-reproduction.json`:

```sh
git -c core.autocrlf=false checkout-index --prefix=<temporary-checkout>/ -- <receipt-files>
git -c core.autocrlf=true checkout-index --prefix=<temporary-checkout>/ -- <receipt-files>
git -c core.autocrlf=true -c core.attributesFile=<candidate.gitattributes> checkout-index --prefix=<temporary-checkout>/ -- <receipt-files>
```

Each checkout is passed to the unchanged `gradient_compute.verify(root)`:

| Policy | Header CRLF count | Result |
| --- | ---: | --- |
| LF checkout (`autocrlf=false`) | 0 | PASS |
| Windows conversion (`autocrlf=true`) | 1224 | Same stale-header failure as hosted Windows |
| Windows conversion plus candidate LF rules | 0 | PASS |

The expected LF header SHA-256 is
`ba22243a438adb8a18e42013e786b1e80fdb6df094dd14d3f668073b36ecbbc7`.
The converted header SHA-256 is
`3da19894d3439241cb9e20f569afb7e3fceb6a0fb56f50025cf8ce0c9870c0f5`.
After the candidate rules produce a passing checkout, the script deliberately
changes the temporary header and verifies that strict stale-byte detection still
rejects it.

## Affected receipt inputs and follow-up repair

`src/field/shaders/manifest.json` contains byte receipts for:

- `src/field/GradientComputeShaders.h`
- `tools/shaders/gradient_compute.py`
- `src/field/shaders/frozen8.comp`
- `src/field/shaders/frozen8.wgsl`
- `src/field/shaders/jacobi.comp`
- `src/field/shaders/jacobi.wgsl`

The verifier also validates byte receipts from
`third_party/vulkan/manifest.json` for `vk_platform.h`, `vulkan_core.h`, and
`LICENSE.txt`. The two manifests are read as JSON and are not themselves hashed.

A minimal follow-up repair is to append `candidate.gitattributes` to the
repository attributes, pinning these receipt-covered text files to LF. The
candidate passes the actual Git conversion reproduction without changing any
receipts, shader bytes, simulation code or runtime dependencies. It has only
been tested in a disposable checkout, and has **not** been applied to the feature
branch. A follow-up should retain this regression coverage and rerun Windows
build/runtime verification. Silently accepting mismatched byte receipts or
regenerating shader receipts from converted text is unnecessary.
