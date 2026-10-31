# Tournament storage verification

Tested commit: `8fc4b9e415c74f6c465c429a411472c7ec8f56ca`.

The JSON receipt records the base revision, Linux/Python environment, exact commands,
exit codes and log hashes. The original recovery pilot emitted226370428bytes;
the probability-only stream retained all504probability records in5715compressed bytes.
`real-log-proof.json` includes source and retained-content hashes. The large source
log remains local; this evidence branch retains only the compact result.

The change is Python orchestration/retention; no simulation code or formats changed.
Other-platform execution and a native build matrix were not run for this tooling-only
change. Tests cover real noisy/file-growing child processes, lossless gzip capture,
probability and tactical record filtering, hard file/attempt limits, interrupted
collection, reservation races, disk pressure on resumed transfers, and durable
spool publication/acknowledgement. Storage-limited measurements are never accepted
as successful or automatically retried.

The local cleanup reclaimed152.3809928894043GiB of redundant per-object checksum
payload across32verified repeated games (34files). Complete compact per-tick traces
and original match saves/replays were retained; the removal journal remains local.
