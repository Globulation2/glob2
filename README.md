# Shared gradient controller: milestone 1 evidence

[Review report](report.md) · [Source and toolchain](source-manifest.json) · [Predeclared criteria](predeclared-protocol.json) · [Raw paired analysis](analysis.json) · [Archive manifest](manifest.json)

Source `9f3c92c834c17f4f15f09a3f0c65edb2997cefd7`, base `69ef3e1b2bf453aa280a69be72f83167b1511d9d`. This orphan evidence branch is separate from PR source history.

Download each part through GitHub's raw/download action:

- [verification.zip.part-00](verification.zip.part-00)

```sh
cat verification.zip.part-* > verification.zip
echo '5af5732048cba78e7ee64b6e968d283fc5f86fb6a9e49c10b96f3620dd20b1a3  verification.zip' | sha256sum --check
unzip verification.zip
python3 restore-duplicates.py
```

The archive contains exact commands, build/test logs, traces, saves, raw measurements, input hashes and affinity restoration receipts. `files.json` records all retained original paths and hashes; `duplicates.json` lists identical payloads restored by the script. Generated executables and disposable profiles are excluded. Paths in original commands refer to the author's checkout and should be adjusted for reproduction. Only Linux/NVIDIA hardware was exercised. Reused map fixtures are accounting controls, not unseen holdouts. Earlier PR evidence remains linked in the report.

Dependency audit: [dynamic library hashes](dependency-manifest.json), collected after measurement. All 37 resolved library modification/change timestamps predate the final timing interval. This supplemental metadata is alongside, not inside, the original verification archive.
