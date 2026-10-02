# Match relay (`glob2-relay`)

`glob2-relay` carries online matches. Clients connect to it over a WebSocket and
speak the binary [turn protocol](turn-protocol.md). The relay checks each player's
signed match ticket, runs one `TurnSequencer` per match, and uploads the match record
to the platform when the match ends. It never simulates the game, and it knows
nothing about sim versions beyond checking that the tickets of a match agree.

The code lives in `src/relay/`. The turn core it hosts lives in `src/net/turn/`.

| File | Role |
| --- | --- |
| `RelayMain.cpp` | Entry point, signals, start-up and shutdown order |
| `RelayConfig.*` | `GLOB2_RELAY_*` environment variables |
| `RelayServer.*` | Listener, HTTP endpoints, WebSocket connections, matches, drain |
| `TicketVerifier.*` | Ed25519 JWT verification with OpenSSL EVP, claim checks |
| `JwksStore.*` | Trusted keys: static file or platform JWKS, refresh on unknown `kid` |
| `MatchDirectory.*` | Admission bookkeeping: live matches, agreement, tombstones |
| `PlatformLink.*` | Registration, heartbeat, setup lookup, record upload, spool |
| `MatchReport.*` | JSON bodies sent to the platform |
| `HttpClient.*` | Asynchronous HTTP/1.1 and HTTPS client for platform calls |
| `RelayMetrics.*` | Counters rendered at `/metrics` |

## Building and running

```sh
scons role=relay release=1 relay      # glob2-relay and glob2-relay-tests
build/<toolchain>/relay/release/src/glob2-relay
```

The relay role builds only the relay. It links the turn core, three libgag stream
sources, OpenSSL, and the header-only Boost.Beast and Boost.Asio. It links no SDL
library, although compiling it needs SDL's headers, because libgag's integer types
come from them. JSON uses the vendored nlohmann/json under `third_party/`.

The relay runs on a single event-loop thread. That thread serializes every call into
every match's sequencer, as `TurnSequencer` requires. A relay process holds many
matches; operators scale out by running more relays. The platform assigns each match
to one relay.

On start-up the relay prints `READY <port>` on standard output. Its logs go to
standard error, one line per event.

## Configuration

Everything is configured through environment variables. A variable ending in `_FILE`
names a file whose contents replace the plain variable, which suits container
secrets.

| Variable | Default | Meaning |
| --- | --- | --- |
| `GLOB2_RELAY_BIND` | `0.0.0.0` | Listen address |
| `GLOB2_RELAY_PORT` | `7495` | Listen port; `0` picks a free port |
| `GLOB2_RELAY_ROUTE` | `/relay` | WebSocket path |
| `GLOB2_RELAY_TLS_CERT`, `GLOB2_RELAY_TLS_KEY` | unset | PEM chain and key. Both set means the relay serves TLS itself; neither means plain TCP behind a TLS-terminating proxy |
| `GLOB2_RELAY_ALLOWED_ORIGINS` | empty | Comma-separated `Origin` values allowed for browser clients; `*` allows any |
| `GLOB2_RELAY_TRUSTED_PROXIES` | empty | Comma-separated proxy addresses whose last `X-Forwarded-For` hop names the client |
| `GLOB2_RELAY_METRICS_TOKEN` (`_FILE`) | unset | When set, `/metrics` requires `Authorization: Bearer <token>` |
| `GLOB2_RELAY_MAX_CONNECTIONS` | `2048` | Match connections in total |
| `GLOB2_RELAY_MAX_CONNECTIONS_PER_ADDRESS` | `32` | Match connections per client address |
| `GLOB2_RELAY_MAX_MATCHES` | `200` | Matches at once; reported as `capacity.maxMatches` |
| `GLOB2_RELAY_HELLO_TIMEOUT_SECONDS` | `10` | Time a new connection has to present a valid `Hello` |
| `GLOB2_RELAY_FRAMES_PER_SECOND`, `GLOB2_RELAY_FRAME_BURST` | `200`, `600` | Token bucket for frames received per connection |
| `GLOB2_RELAY_MAX_OUTGOING_BYTES` | `8388608` | Send backlog after which a connection that does not read is dropped |
| `GLOB2_RELAY_GRACE_SECONDS` | `180` | Reconnect grace before the relay sequences a seat's quit (not waited out once a client has left with `Quit(GameFinished)` and nobody is connected; see turn-protocol.md) |
| `GLOB2_RELAY_DRAIN_TIMEOUT_SECONDS` | `14400` | Longest drain before the remaining matches are aborted; `0` waits forever |
| `GLOB2_RELAY_JWKS_FILE` | unset | Static JWKS file, for tests and set-ups without a platform |
| `GLOB2_RELAY_JWKS_URL` | see below | JWKS location |
| `GLOB2_RELAY_JWKS_REFRESH_SECONDS` | `600` | Periodic JWKS refresh |
| `GLOB2_RELAY_JWKS_MIN_REFRESH_SECONDS` | `10` | Minimum time between refreshes caused by unknown key ids |
| `GLOB2_RELAY_TICKET_LEEWAY_SECONDS` | `30` | Clock skew allowed on `exp` and `nbf` |
| `GLOB2_RELAY_TICKET_ISSUER` | unset | When set, tickets' `iss` must equal it |
| `GLOB2_RELAY_PLATFORM_URL` | unset | Platform origin for `/internal/v1` calls; unset disables them |
| `GLOB2_RELAY_PLATFORM_CA` | system store | Extra trust anchors for HTTPS calls to the platform |
| `GLOB2_RELAY_KEY` (`_FILE`) | required with a platform | Bearer token for `/internal/v1` calls |
| `GLOB2_RELAY_ID` | host name | `relayId`, `[A-Za-z0-9._-]{1,64}` |
| `GLOB2_RELAY_PUBLIC_URL` | required with a platform | The `wss://` URL clients use, sent at registration |
| `GLOB2_RELAY_REGION` | `default` | Region id, `^[a-z0-9][a-z0-9-]{0,31}$` |
| `GLOB2_RELAY_SPOOL_DIR` | unset | Directory where records wait until they are uploaded |
| `GLOB2_RELAY_UPLOAD_ATTEMPTS` | `8` | Upload attempts per match, with backoff from 1 s up to 60 s |

At least one of `GLOB2_RELAY_JWKS_FILE`, `GLOB2_RELAY_JWKS_URL` or
`GLOB2_RELAY_PLATFORM_URL` must be set. The JWKS location is the first of these that
applies: the static file, the explicit URL, the `jwksUrl` from the latest registration
response, or `<platform>/.well-known/jwks.json`.

TLS is optional because the standard deployment terminates TLS at Caddy, which then
forwards `/relay` to the relay. A relay listening without TLS must be reachable only
from the proxy, through a private network or firewall. In that case, list the proxy
in `GLOB2_RELAY_TRUSTED_PROXIES` so per-address limits apply to real clients.

## Endpoints

| Path | Response |
| --- | --- |
| `GLOB2_RELAY_ROUTE` (WebSocket) | The match connection |
| `GET /healthz` | `200 ok`, or `200 draining` while draining. Liveness only |
| `GET /readyz` | `200 ready`; `503` while draining or before any ticket key is loaded |
| `GET /metrics` | Prometheus text format; bearer token if `GLOB2_RELAY_METRICS_TOKEN` is set |

A request that is not a WebSocket upgrade gets one response and then the connection
closes. An upgrade is refused with an HTTP status before the WebSocket opens in these
cases:

| Status | Reason |
| --- | --- |
| `404` | Wrong path |
| `403` | An `Origin` header that is not allowed |
| `400` | Pipelined data, or a malformed `X-Forwarded-For` from a trusted proxy |
| `429` | Per-address limit |
| `503` | Total connection limit |

Everything after the upgrade happens in turn-protocol messages.

## Match connections

Messages travel exactly as `NetConnection` sends them over `WssTransport`. Each
payload is prefixed with a 2-byte big-endian length, and the length-prefixed frames
form a byte stream carried in binary WebSocket messages. The relay accepts frames
split across messages, or several in one message. It sends each frame as one message.
Text messages, empty frames and incomplete frames that grow beyond two maximum-size
frames close the connection with `Reject(6)`.

The first frame must be `Hello`. Reading pauses while the relay verifies the ticket,
so frames that follow the `Hello` wait in the socket and reach the match in order.

1. **Protocol version.** A different version gets `Reject(1)`.
2. **Ticket.** See [tickets](#tickets). A refused ticket gets `Reject(2)` with the
   reason in the detail, for example `Ticket refused: expired`.
3. **Admission.** The ticket's `matchId` names the match:
   - The first valid ticket for an unknown `matchId` creates the match. The
     sequencer's human seats come from that ticket's `humanSeats`.
   - Every later ticket must carry the same `simVersion` and `humanSeats`, or it
     gets `Reject(2)`.
   - A match that has ended keeps a tombstone until every ticket seen for it has
     expired (at least one hour), so a late or reused ticket gets `Reject(5)` and
     cannot start the match again.
   - A draining relay, or one at `GLOB2_RELAY_MAX_MATCHES`, refuses new matches with
     `Reject(5)` and a detail that asks the client to request another relay. It still
     admits reconnects to the matches it is running.
4. The `Hello` then goes to the match's sequencer, which answers with `Welcome` or
   refuses the seat (`Reject(4)` for a seat that has left).

A connection that sends no valid `Hello` within `GLOB2_RELAY_HELLO_TIMEOUT_SECONDS`
is closed. Each connection has a frame-rate token bucket; a client that exceeds it
gets `Reject(7)`. A client that stops reading until its send backlog exceeds
`GLOB2_RELAY_MAX_OUTGOING_BYTES` is disconnected. The sequencer then treats it like
any other transport loss, and the client resumes from its horizon when it reconnects.
WebSocket keep-alive pings detect dead peers after 30 s of silence. Clients ping every
500 ms anyway.

A timer drives each match's `update` every 10 ms, a quarter of a tick.

## Tickets

Tickets are EdDSA (Ed25519) JWTs defined in `platform/packages/protocol/src/ticket.ts`.
`TicketVerifier` makes the same checks, in the same order, with the same reason
names as the platform's `verifyJwt`:

1. `malformed`: not three base64url segments of JSON;
2. `algorithm`: `alg` is not `EdDSA`;
3. `type`: `typ` is not `glob2-match+jwt`, so access tokens (`at+jwt`) are refused;
4. `key`: missing or unknown `kid`;
5. `signature`: the signature does not verify;
6. `audience`: `aud` is not `glob2-relay`;
7. `expired`: `exp + leeway ≤ now`;
8. `not_yet_valid`: `nbf − leeway > now`.

The relay then checks the claims' shape, and reports a failure as `claims`:

- `matchId`, `sub`, `accountId` and `jti` are lowercase UUIDs, and `sub` equals
  `accountId`;
- `seat` and every entry of `humanSeats` are 0–11, `humanSeats` is unique and
  non-empty, and `seat` is one of them;
- `simVersion` is exactly `{versionMinor, netProtocol, dataHash}`;
- `relayUrl` is an http(s) or ws(s) URL;
- `iss` equals `GLOB2_RELAY_TICKET_ISSUER` when that is set.

When `GLOB2_RELAY_PUBLIC_URL` is set, a ticket's `relayUrl` must equal it exactly
(`Reject(2)`, `Ticket is for another relay`), so a ticket works only on the relay the
platform allocated the match to. The platform must therefore put the registered
`publicUrl` into tickets verbatim.

`entitlements` is ignored.

Base64url decoding is strict: padding, invalid characters and non-zero trailing
bits are all refused. Only `OKP`/`Ed25519` keys with `use` `sig` (or no `use`) are
taken from a JWKS. Other keys are skipped.

**Key rotation.** A ticket whose `kid` is unknown triggers a JWKS refresh before it
is refused. The refresh is shared by concurrent tickets and happens at most once per
`GLOB2_RELAY_JWKS_MIN_REFRESH_SECONDS`. A static JWKS file is re-read the same way,
so replacing the file rotates keys without a restart. The platform should publish
a new key before signing with it, and keep the old key until every ticket signed
with it has expired.

`test/fixtures/relay-tickets/` holds copies of the protocol package's ticket fixtures
(see `manifest.json` there). `glob2-relay-tests` checks that every fixture verifies,
or fails with the manifest's reason.

## Platform calls

When `GLOB2_RELAY_PLATFORM_URL` is set, the relay calls the platform's internal API.
The shapes are defined in `platform/packages/protocol/src/relay.ts`. Every call
carries `Authorization: Bearer <GLOB2_RELAY_KEY>`.

| Call | When |
| --- | --- |
| `POST /internal/v1/relays/register` (`RelayRegistration`) | At start-up, after a failed registration (backoff from 1 s up to 30 s), and when a heartbeat asks for it |
| `POST /internal/v1/relays/heartbeat` (`RelayHeartbeat`) | Every `heartbeatIntervalSeconds` from the registration response (default 15 s), and at once when draining starts |
| `GET /internal/v1/matches/{matchId}/setup` | When a match starts, and again at the end if the first lookup failed |
| `PUT /internal/v1/matches/{matchId}/record` | Match end: the `MatchRecord` bytes, `Content-Type: application/vnd.glob2.match-record` |
| `POST /internal/v1/matches/{matchId}/end` (`RelayMatchEnded`) | After the record upload succeeds |

A heartbeat answered with `404`, or with `"reregister": true`, makes the relay
register again.

**Registration.** `build` is `glob2-relay <version>` and `turnProtocol` is the turn
protocol version. `load` reports the current matches and connections. The relay does
not report `cpu`.

**Setup.** The relay stores the response body verbatim as the record's `setupJson`.
It reads `map.hash` from that body as the record's `mapHash`. If no setup is
available, both stay empty or zero, and the verifier has to get the setup from the
platform's own copy.

**Match end.** `RelayMatchEnded` is filled from the record:

- `seats[].disconnects` counts disconnect events.
- `quitTick` is the tick at which the seat left, by quit or grace expiry.
- `droppedForDesync` and `desync.minoritySeats` list seats told to rejoin.
- `desync.flagged` is the record's flag.
- `reason` is:
  - `completed` when a client sent `Quit` with reason "game finished";
  - `abandoned` when every human left otherwise;
  - `aborted` when the relay ended the match (drain timeout or a second signal).
    An aborted record also carries the incomplete flag.

**Retries and the spool.** Uploads retry with backoff up to
`GLOB2_RELAY_UPLOAD_ATTEMPTS` times. With `GLOB2_RELAY_SPOOL_DIR` set, the relay
writes `<matchId>.g2mr` and `<matchId>.end.json` there before the first attempt, and
deletes them after a successful upload. On start-up it re-submits every spooled match.
The platform must therefore accept a repeated `PUT` and `end` for the same match. A
relay without a platform keeps records only in the spool.

## Drain and shutdown

The first `SIGTERM` or `SIGINT` starts a drain:

- `/readyz` turns `503` and a heartbeat reports `"draining": true` at once.
- No new match starts, but running matches continue and accept reconnects.
- When the last match has ended and its record has been handed to the uploader, the
  relay waits up to 30 s for uploads (spooled records survive beyond that) and exits
  with status 0.
- `GLOB2_RELAY_DRAIN_TIMEOUT_SECONDS` bounds the drain. When it expires, the relay
  aborts the remaining matches.

A second signal aborts every match at once, then shuts down the same way.

## Metrics

All metric names start with `glob2_relay_`.

| Kind | Metrics |
| --- | --- |
| Gauges | `connections`, `matches`, `draining`, `registered`, `pending_uploads`, `jwks_keys` |
| Connection counters | `connections_accepted_total`, `connections_refused_total`, `frames_in_total`, `frames_out_total`, `bytes_in_total`, `bytes_out_total`, `slow_readers_dropped_total`, `flooding_dropped_total` |
| Match counters | `matches_started_total`, `matches_ended_total{reason}`, `orders_sequenced_total`, `bundles_sent_total` (the last two are added when a match ends) |
| Ticket counters | `tickets_rejected_total{reason}`, labelled with a ticket reason above, `wrong_relay`, `sim_version_differs` or `human_seats_differ` |
| Platform counters | `jwks_refresh_ok_total`, `jwks_refresh_failed_total`, `registrations_ok_total`, `registrations_failed_total`, `heartbeats_ok_total`, `heartbeats_failed_total`, `uploads_ok_total`, `uploads_failed_total` |

## Tests

```sh
scons role=relay release=1 relay
build/<toolchain>/relay/release/src/glob2-relay-tests
python3 -m unittest discover -s tests/relay -v
```

`glob2-relay-tests` (doctest) covers:

- every ticket fixture;
- tickets signed with the protocol package's TEST-ONLY fixture key: leeway
  boundaries, `nbf`, access tokens, claim shape, issuer and key rotation;
- JWKS parsing and base64url;
- match admission and tombstones;
- the platform JSON bodies;
- the configuration checks.

`tests/relay/` starts the real binary. Its Python helpers, which use only the
standard library, sign tickets with the same fixture key in pure Python, speak the
turn protocol over a WebSocket, parse match records and run a fake platform. The
tests cover:

- **A full match.** Three clients exchange orders and voice. The killed client
  resumes from its horizon, without lost or duplicated orders. The record and
  `RelayMatchEnded` must match what the clients saw.
- **Refusals.** Every bad fixture, protocol version, disagreeing tickets, Origin,
  route and the metrics token.
- **Key rotation and drain.** An unknown `kid` refreshes the JWKS. During drain, no
  new matches start, a reconnect works and the relay exits 0.
- **TLS.** WSS clients and HTTPS platform calls, with a private CA from
  `deploy/provision_tls.py`.

CI runs both in the native-programs job.

## Limits and follow-ups

- In `deploy/compose.yaml` the relay runs behind Caddy without TLS of its own. Each
  replica is reached at `/relay/<relay id>`, its id being its container's host name,
  and Caddy rewrites the path to `/relay` (see the
  [self-hosting guide](../hosting/README.md)).
- The setup lookup endpoint and the idempotent upload behaviour are relay-side
  assumptions that the platform API must implement (see [platform calls](#platform-calls)).
- A refused new match uses `Reject(5)` (match over) with an explanatory detail. A
  dedicated "relay unavailable" reason would let clients ask for another relay
  automatically; it needs a turn protocol version bump.
- The relay has been run on macOS (clang) and Linux (g++-13). It does not build on
  Windows, and is not meant to.
