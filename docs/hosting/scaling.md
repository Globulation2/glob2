# Scale the online stack

Replica placement, relay draining and service capacity.

## Scaling

Change the replica counts in `.env` and run `docker compose up -d`.

- **platform-api** is stateless; Caddy re-resolves the replicas every few seconds.
  Realtime sockets on a removed replica reconnect to another. Room presence is
  shared: each replica registers in `api_replicas`, heartbeats every 10 seconds and
  records its sockets' accounts in `realtime_presence`, so a player counts as
  connected while any replica holds a socket of theirs. A replica that stops
  heartbeating for 45 seconds (crashed, or cut off from the database) is expired by
  the others, and its players are marked disconnected in their rooms.
- **platform-worker** can run several replicas: every replica applies job results,
  one at a time holds the scheduler (matchmaker, ratings, warm maps).
- **engine-agent** replicas lease jobs from the same queue (`engine_jobs`, through
  `platform-api`); each runs `ENGINE_CONCURRENCY` jobs and polls every
  `ENGINE_POLL_MS` (1000) when idle.
  Each engine process may use up to 4-8 GB of address space
  (`ENGINE_MEMORY_MB`), so size concurrency by memory.
- **relay**: each replica registers its own URL, `wss://<host>/relay/<container id>`,
  derived from `GLOB2_PUBLIC_ORIGIN` and its container's host name, under a stable
  relay id: the first free slot `relay-1`, `relay-2`, … it claims (with `flock`) on
  the `relay-spool` volume for as long as it runs (`deploy/relay-entrypoint.sh`).
  A recreated relay therefore gets an id from the same set and re-submits what it
  had spooled, and pinned keys (`relay-1:<key>`) keep matching. At start-up a
  relay also adopts spooled matches of directories no running relay holds (after
  a scale-down, or from before stable ids); the platform accepts repeated uploads.
  Adding replicas is immediate. Removing one ends its matches unless it is drained
  first; use the [relay drain procedure](upgrades.md#draining-relays) rather than lowering the
  count.

Relays on other hosts (to be closer to players) are not part of this file. Such a
relay needs its own TLS or proxy and public URL (`GLOB2_RELAY_PUBLIC_URL`), the relay
key, and a private route to `platform-api`'s `/internal` (a VPN or private network:
`/internal` is never served publicly). Settings are in [relay](../multiplayer/relay.md).

[Hosting index](README.md) · [Documentation index](../README.md).
