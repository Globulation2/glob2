# Turret optimization evidence

Read [report.md](report.md) for individually measured optimizations and [integrated-report.md](integrated-report.md) for the later merged-head rerun. The datasets use different binaries and must not be pooled.

The prototype stays in draft and cached firing remains benchmark-only. Save continuation and live qualification remain incomplete.

The archive contains exact binaries, logs, commands, host samples, scripts, checksums, orders, saves and original contended cohorts. Reassemble the parts in numeric order:

```sh
cat optimization-evidence.tar.gz.part-* > optimization-evidence.tar.gz
sha256sum optimization-evidence.tar.gz
tar -xzf optimization-evidence.tar.gz
```

Check the archive and each part against manifest.json. Generated evidence is on this dedicated branch, not the feature branch.
