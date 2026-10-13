# Turret owner-cost follow-up

[Analysis and validation](report.md) · [CPU scopes](owner-cpu.png) · [whole-engine effects](native-delta.png).

Three implementations: previous prototype, preparation only, preparation plus publication. Read the controlled-relink provenance carefully; shared harness banners do not describe the old implementation objects. Exact source snapshots, compiler/link commands, binaries, Callgrind dumps, native samples, host activity and correctness evidence are archived.

Reassemble and verify against manifest.json:

```sh
cat owner-cost-evidence.tar.gz.part-* > owner-cost-evidence.tar.gz
sha256sum owner-cost-evidence.tar.gz
tar -xzf owner-cost-evidence.tar.gz
```

This is focused four-participant/eight-entry evidence, not full live qualification. Shipping targeting and hiring stay unchanged; cached continuation failures and other coverage gaps remain.
