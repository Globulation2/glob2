# GPU gradient verification evidence

Source PR: https://github.com/Globulation2/glob2/pull/1045
Tested commit: 0003dbef3 (full SHA in manifest).

This orphan evidence branch contains generated review artifacts only, separate from source history.

Download verification.zip, extract, then run `python3 restore-duplicates.py` to restore byte-identical saves/replays omitted from the archive to avoid repeated storage. The duplicate-files.json records every alias.

Archive SHA256: a1f219639a80db113d074d73fef736c71ed9243d277235834efdb2d81d41f802

Start with parallel-lanes/report.md and manifest.json. Logs, JUnit results, per-tick checksums, replay files, save/checkpoint files, raw match metrics, fixtures, diagnostic event timelines and commands are included. Production binaries and diagnostic tool distributions are omitted. Paths in manifests record the original development environment.

Local acceptance and unverified platform coverage are described in the PR comment; this archive does not establish cross-platform execution equivalence.
