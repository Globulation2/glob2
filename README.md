# PR1045 verification evidence

Source commit: `c2ab2628bad246b616d0cd03d73fb03533877503`. This orphan evidence branch is separate from the source PR history.

[Consolidated review report](report.md) · [Source/build/test/archive manifest](manifest.json)

Archive: 105,040,416 bytes. SHA256: `94f77bfb75ee1af8d4283124647441b0bcce97fcc6eb89376c99005266f257e0`.

Download the archive file(s):

- [verification.zip.part-00](verification.zip.part-00)
- [verification.zip.part-01](verification.zip.part-01)
- [verification.zip.part-02](verification.zip.part-02)

If viewing a file on GitHub, use its download/raw action. Verify and extract in an empty temporary directory:

```sh
cat verification.zip.part-* > verification.zip
echo '94f77bfb75ee1af8d4283124647441b0bcce97fcc6eb89376c99005266f257e0  verification.zip' | sha256sum --check
unzip verification.zip
python3 restore-duplicates.py
```

The restored `artifacts/` tree uses repository-relative paths. For reproduction, copy it into a checkout of the stated source revision. Retained scripts contain exact original commands and local prefix/fixture paths; adjust those paths for another checkout. The archive includes source snapshots, models, OpenCL sources, actual driver disassembly, raw timings, source/input hashes, JUnit/logs, fixtures, saves, replay bytes, per-tick traces, affinity guards and cleanup receipts. Generated executables, virtual environments and binary caches are excluded.

`files.json` contains SHA256 and size for every retained original file. `duplicates.json` maps omitted duplicate paths to their stored payload. `excluded-build-artifacts.json` lists excluded binary build outputs. Historical evidence remains historical; the consolidated report identifies tested final source and separates prior campaigns.

The CPU affinity reservation was restored after timed work. Both GPUs shared desktop graphics; cooperative locks never established exclusive-GPU execution. Only Linux/NVIDIA was exercised. See the report for coverage and limitations.
