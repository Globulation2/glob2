# Gradient optimization evidence for PR #791

The latest independent review and cleanup is documented in [POLISH.md](POLISH.md), for product commit `478b9b4bae0aaf177b40c395f44466e8d2aa3152`. Its rebuilt game executable is identical to the previously measured binary.

Review [REPORT.md](REPORT.md) for results, limits, implementation decisions and validation. This is an evidence-only orphan branch; it contains no product history and is not intended to merge.

- Candidate: `37d609766374e7efc1679d643c7d18cde84364ab`
- Baseline: `3266c8e518a0d40b2100e2e36596822ad74a6f0b`
- Product PR: https://github.com/Globulation2/glob2/pull/791
- Parent ecology PR: https://github.com/Globulation2/glob2/pull/787

## Retrieve and inspect

Clone this branch into a separate directory, or download its files. The archive is split into ordered parts smaller than 50 MiB to keep individual Git objects manageable. `archive-manifest.json` provides SHA-256 hashes of the archive and every part.

```sh
cat evidence.tar.gz.part* > evidence.tar.gz
sha256sum evidence.tar.gz
# Compare the hash with archive-manifest.json.
tar -xzf evidence.tar.gz
```

The archive stores content by hash to avoid repeating byte-identical simulation traces and saves. `gradient-evidence/files.json` maps original repository-relative paths to hashes and sizes. From a separate Globulation 2 checkout, restore them with:

```sh
python3 /path/to/extracted/gradient-evidence/restore.py
```

This writes evidence under ignored `artifacts/` directories in the current checkout and verifies each content hash. The restored archive includes input maps, raw samples, profiles, manifests, compiler commands, copied benchmark sources, reviews, sanitizer/QEMU logs, per-tick checksums, replays and saved games. Production and test executables are identified by hash, not bundled. The report indexes the authoritative final results separately from exploratory experiments.

Regenerate the report from restored measurements, without running benchmarks:

```sh
python3 artifacts/gradient-optimization/published/generate-report.py
```

Full-engine measurements are Linux x86-64 only, on a shared development host. ARM64 emulation verifies tested NEON arithmetic but cannot establish real ARM performance. Read the report's uncertainty intervals and omitted platform coverage before interpreting speed claims.
