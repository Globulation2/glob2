# PR 1045 final master comparison

[Results and limits](report.md) · [Every measured game](all-games.csv) · [Protocol](protocol.json) · [Environment](metadata.json) · [Archive manifest](manifest.json)

Exact PR `13843165bd7e778650142fe884c7af37b40b56f1`, master `07442b5919988edb323efdf571f220e657a30492`. This orphan evidence branch is separate from PR source history.

Download archive parts through GitHub's raw/download action:

- [verification.zip.part-00](verification.zip.part-00)
- [verification.zip.part-01](verification.zip.part-01)
- [verification.zip.part-02](verification.zip.part-02)
- [verification.zip.part-03](verification.zip.part-03)
- [verification.zip.part-04](verification.zip.part-04)
- [verification.zip.part-05](verification.zip.part-05)
- [verification.zip.part-06](verification.zip.part-06)
- [verification.zip.part-07](verification.zip.part-07)
- [verification.zip.part-08](verification.zip.part-08)
- [verification.zip.part-09](verification.zip.part-09)
- [verification.zip.part-10](verification.zip.part-10)

```sh
cat verification.zip.part-* > verification.zip
echo '761bbc475061ecd9a4484d9966d22ba4ddbe71af91a45bfb40d08ea8f05b4bb4  verification.zip' | sha256sum --check
unzip verification.zip
python3 restore-duplicates.py
```

The archive contains immutable saves, all measured outputs, excluded trials, release build logs, commands and scripts, protocol/source/dependency hashes, and CPU reservation/cleanup receipts. Binaries, disposable profiles and unrelated full process command lines are omitted. Original commands use local paths that must be remapped. Only Linux/NVIDIA execution is covered. No new algorithms or live tuning were added. Automatic is an established CPU default, not learned selection.
