# PR 1045 final verification evidence

[Results and limitations](report.md) · [Source and environment](source-manifest.json) · [Archive manifest](manifest.json)

Source `0520c89a91930362e0cd4407ba42a677365d1226`, base `4ca7b086359891010281dd994dbbcd8cceadadad`. This orphan evidence branch is separate from PR source history.

Download archive parts through GitHub's raw/download action:

- [verification.zip.part-00](verification.zip.part-00)

```sh
cat verification.zip.part-* > verification.zip
echo '94056a9abbca6ac73e67c95e07053ab83a45e3a4c89ab7868b9a4b3f22cfe8d6  verification.zip' | sha256sum --check
unzip verification.zip
python3 restore-duplicates.py
```

The archive includes commands, release build/test logs, JUnit reports, per-tick traces, saves, replays, qualification raw records and frozen receipts, reproduction sources and file hashes. Generated binaries and disposable profiles are excluded. Original commands use local paths that must be adjusted. One unchanged PNG unit case fails; only Linux/NVIDIA execution is verified. No final holdouts or new algorithms were admitted.
