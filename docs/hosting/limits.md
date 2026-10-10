# Deployment limits

Known deployment constraints to account for before operating an instance.

## Limits

- One host. Postgres, the blob volume and Caddy are single instances; S3-compatible
  blob storage is not implemented yet (`BLOB_STORE=s3`; the `BlobStore` interface
  includes the optional `list()` the blob collector uses for stored files no row
  names).
- Relays on other hosts need [manual setup](scaling.md).
- The engine reports its simulation version through `glob2 info sim-version --format json`.
  The engine-agent image also carries the version computed by
  `deploy/sim_version.py` at build time; startup checks that the engine and image
  identities agree.
- The relay key is one shared secret for all relays; the engine-agent key likewise
  for all agents. A compromised agent (the engine runs untrusted files) can still
  report forged results for jobs it leases, such as match verdicts.
- Abuse limits that guard sign-in, password guessing, guests, browser sign-in
  attempts, uploads, catalog writes and chat are shared by all API replicas
  (Postgres counters). The general per-address request cap
  (`limits.apiPerMinute`) and the realtime connection and message limits are per
  replica.

[Hosting index](README.md) · [Documentation index](../README.md).
