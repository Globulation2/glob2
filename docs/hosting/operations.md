# Operate and verify an instance

Health checks, retention and whole-stack verification. Run destructive tests on a disposable deployment.

## Operations

Run the Compose commands in this section from `deploy/`.

- Logs: `docker compose logs -f platform-api relay`; Compose keeps up to 100 MB per
  container (local driver, five 20 MB files).
- Relay metrics are Prometheus text at `http://<relay>:7495/metrics` on the backend
  network (not public).
- Admin CLI: `docker compose run --rm --no-deps platform-api platform admin grant|revoke|ban|delete …`
  ([what deleting keeps and removes](../multiplayer/account-management.md#administration)).
- Failed verifications: a match whose verify job failed or was lost is marked
  `failed` and not rated, and the worker logs `match verification failed` at error
  level (alert on it). List them with `platform matches failed` and re-run one with
  `platform matches reverify <match id>` (`--force` replaces a queued job), or
  `POST /api/v1/admin/matches/<id>/reverify` as an administrator
  ([details](../multiplayer/ratings-and-matchmaking.md#applying-a-verdict-exactly-once)).

### Retention

The worker leader deletes old rows every minute (`apps/worker/src/maintenance.ts`,
at most 1000 rows per table and run) and collects blobs every six hours:

| Data | Kept |
| --- | --- |
| Refresh tokens | rotated or revoked: 7 days (reuse detection); expired: 30 days after expiry |
| Web sessions, provider sign-in flows | 30 days after expiry or revocation; 24 hours after expiry |
| Browser sign-in attempts | 7 days once finished |
| Guest accounts | deleted when unused for 90 days (no sign-in or realtime session, no device use) and they never played a match, host no open room and own no catalog map |
| Room chat | 30 days |
| Engine jobs | 30 days after completion; each match's latest succeeded verify job stays |
| Match proposals, finished queue tickets | 30 days |
| Engine agents not seen | 7 days |
| Spilled NOTIFY payloads | 1 hour |
| Blobs | unreferenced ones (no map version, preview, match artifact, upload, generated map (warm pool maps included), or match played on the map) 7 days after creation; stored files no `blobs` row names, 7 days after they were written |

Matches, participants, ratings, rating history, catalog maps, map download counts
(by account, or by IP address for downloads without an account) and the audit log
are kept. The official instance's [privacy policy](../mobile/privacy-policy.md)
states these periods to players; change it together with `maintenance.ts`.

- Migrations: `docker compose run --rm --no-deps init node packages/db/src/cli.ts status`.
- Stopping: `docker compose stop` drains relays (up to `GLOB2_RELAY_STOP_GRACE`);
  `docker compose down` keeps volumes; `down --volumes` deletes all data.


## Testing a deployment

Run the Python commands below from the repository root. The `--psql` command
passed to `live_match_e2e.py` needs an explicit Compose file when run there:

```sh
python3 test/deployment/platform_stack_smoke.py --log-dir artifacts/platform-stack
```

The smoke test builds the images, starts an isolated project with its own ports,
volumes and `.env`, and checks: every service healthy and `init` successful; TLS
from the local CA, HTTP redirect, the web app, invite pages at `/j/` and `/play/`, and that
`/internal`, `/healthz`, `/readyz` and `/metrics` are not public; the instance
listing the agent's sim version; guest sign-in and `session.hello` over
`/realtime` (and a foreign `Origin` refused); the JWKS against the key files and the
token's `kid`; each relay registered with the platform under its public URL,
reachable at `/relay/<id>`, and unknown ids refused; and a `generate-map` job run by the engine agent
with the real binary, applied by the worker and stored as a blob. It then removes
the project and its volumes. `--no-build --tag <tag>` reuses built images; `--keep`
leaves the stack running. Hosted verification selects this coverage when
requested for affected deployment boundaries or full runs; routine PR checks
remain lightweight. See [verification policy](../../AGENTS.md#validation-and-ci-feedback).
`--match-e2e` also plays a rated quick match through the stack; see
[End-to-end test of the stack](operations.md#end-to-end-test-of-the-stack).

On a running deployment, the same checks run against the public origin with a
publicly trusted certificate, the deployed web client and the replica counts in
the env file (run on the host; nothing is started or removed):

```sh
python3 test/deployment/platform_stack_smoke.py --attach glob2-platform \
    --env-file /path/to/deployment.env --log-dir artifacts/live-smoke
```

`test/deployment/live_match_e2e.py` then plays a real match on the instance: two
guests create and join a room by invite code, start it with AI seats on a generated
map, and two headless native clients (`glob2 online turn-client`, built from the same
sim version) play it through the relay until a sudden-death rule ends it. It checks
that both clients' per-tick checksums agree, and, with `--psql`, that the relay
reported the match, uploaded its record and the verify-match job judged it
`verified`:

```sh
python3 test/deployment/live_match_e2e.py --origin https://play.example.org \
    --glob2 build/linux/client/release/src/glob2 --out artifacts/live-e2e \
    --psql "docker compose -f deploy/compose.yaml -p glob2-platform exec -T postgres psql -U glob2 -d glob2 -At"
```

`--mode queue --queue <id>` plays a rated quick match instead: two new local
accounts (the instance needs local sign-in) join a rated 1v1 queue, accept the
ranked prompt and play. Player A quits first, which leaves B the winner (B
would quit 30 s later otherwise), so the verified match is rated; with `--psql` the script also checks
that both players' ratings on the queue's ladder changed. `--engine-command`
replaces `--glob2` with a command prefix, for example a `docker run` of the
engine-agent image, and `--ca-file` trusts a private CA.

### End-to-end test of the stack

One command builds the stack from this checkout, starts it, plays a rated quick
match through it and tears it down again. It needs a Linux machine with Docker
(Compose v2) and Python 3; the clients use host networking, so Docker Desktop on
macOS does not work.

```sh
python3 test/deployment/platform_stack_smoke.py --jobs 8 --log-dir artifacts/stack-e2e --match-e2e
```

After the smoke checks above, it runs `live_match_e2e.py --mode queue` against the
fresh stack:

1. The instance has local sign-in and a small rated queue (`e2e-ranked`, one
   128×128 generated map).
2. Two new local accounts join the queue, get the ranked accept prompt and accept.
3. Two headless clients (`glob2 online turn-client`) play the match through a relay. They
   run from the engine-agent image, so client, relay and verifier share one build
   and sim version. Their per-tick checksums must agree.
4. Player A quits after 40 s, so B wins and the game ends (B would quit 30 s later
   otherwise). The relay uploads the match record and reports the end.
5. The verify-match job must judge the match `verified`. The worker must then apply
   ratings: `rating_status = applied`, and one won and one lost `rating_history` row
   on the `e2e-ranked` ladder, both with changed μ.

The first run builds every image (about 15 minutes on 4 cores); later runs on the
same Docker host reuse the BuildKit caches. The match adds about three minutes.
Everything ends up in the log directory: the compose logs, `results.json` of the
smoke checks, and under `match-e2e/` the clients' logs, results and checksum
traces, plus the end-to-end `results.json`. The exit status is 0 only if every
check passed. Add `--keep` to leave the stack running for inspection.

The web app's browser suites (`platform/apps/web/e2e`: page smoke tests and axe
accessibility checks on desktop and phone) run separately, against a seeded API:
`npm run build -w @glob2/web && npm run e2e -w @glob2/web` in `platform/`, with the
test Postgres running.

[Hosting index](README.md) · [Documentation index](../README.md).
