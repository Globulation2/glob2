# Development WebSocket gateway

This is transport infrastructure, not yet a supported multiplayer release.
The browser YOG entry now uses WebSocket transport and the same message codecs
as native TCP clients. The legacy YOG handshake, successful account login, and lobby exit are exercised
against a native server through the gateway. Browser chat stays within YOG;
the optional native IRC bridge is disabled. Exact protocol-version admission and
short matching-checksum browser/browser and browser/native matches are tested.
Complete-match qualification and safe handling remain release gates. Account
modernization, invitation rooms and coordinated recovery are deferred under the
[amended delivery scope](implementation.md); refresh/disconnect can end participation.

## Build and run

Install a C++20 compiler and Boost development headers (Beast and Asio).

```sh
scons role=gateway release=1 -j4
scons role=server release=1 -j4
scons role=router release=1 -j4
```

On Linux, the gateway executable is
`build/linux/gateway/release/glob2-ws-gateway`; replace `linux` with `darwin`
on macOS. The lobby and router executables are respectively
`build/linux/server/release/src/glob2-server` and
`build/linux/router/release/src/glob2-router`.

```sh
build/linux/gateway/release/glob2-ws-gateway \
  --listen 127.0.0.1 --port 8080 \
  --origin http://127.0.0.1:8765 \
  --lobby-host 127.0.0.1 --lobby-port 7489 \
  --router-host 127.0.0.1 --router-port 7491
python3 -m unittest discover -s tests/gateway -v
```

The test runner starts its own gateway on an ephemeral port and two gateway
routes backed by an isolated TCP echo service. `GLOB2_GATEWAY` overrides the
binary location. It does not connect to a public YOG server.

The headless router connects to the lobby on loopback by default. Set
`GLOB2_YOG_HOST` to the private lobby hostname for a distributed deployment.
The lobby/router control connection uses the existing internal TCP port 7490.

## Transport contract

- `/yog` and `/router` upgrade to binary WebSockets. They forward only to the
  configured lobby and router. URLs cannot choose arbitrary TCP destinations.
- WebSocket messages carry bytes of the TCP stream. Neither WebSocket messages
  nor TCP reads define game-protocol message boundaries. Clients must buffer
  and decode the game framing independently.
- A WebSocket message is limited to 64 KiB. A TCP read is limited to 16 KiB.
  Each direction has one read/write chain, so a blocked destination stops
  further reads. Large protocol transfers must be chunked above this layer.
- The gateway accepts at most 256 connections, including HTTP requests.
  HTTP headers are limited to 8 KiB. Initial HTTP and backend connection
  operations have ten-second timeouts; established WebSockets use Beast's
  server timeout policy.
- A present browser Origin must match `--origin` exactly. Origin is not
  authentication; native clients may omit it. Credentials belong in the
  versioned YOG protocol, never in a gateway URL.
- `/healthz` checks process responsiveness; it does not assert backend health.
  `/metrics` provides connection, admission and forwarded-byte counters.

The listener is plain HTTP/WebSocket on loopback by default. Public deployment
requires a TLS reverse proxy, private backend ports, and restricted metrics
routing. The [development Compose package](../../deploy/README.md) runs the
proxy, assets, gateway, lobby, and router together. Release packaging and upgrade
qualification remain outstanding.

The gateway neither owns rooms nor simulates a match. Losing the gateway
currently closes the corresponding TCP connections; reconnect semantics must
be implemented in YOG and the clients before this is advertised for play.

## Browser client routing

By default the browser connects to `/yog` and `/router` on its own origin, using
WSS for HTTPS pages and WS for localhost HTTP development. Configure the reverse
proxy to serve game assets and forward those two paths to the gateway. Internal
lobby/router TCP addresses sent by the legacy server are not browser destinations.

For a separate gateway origin, set this deployment configuration before the game
starts (for example in a script loaded by the page):

```js
globalThis.glob2Config = {websocketBase: 'wss://games.example.org'};
```

The gateway still requires the page's origin through `--origin`. The setting
contains no password, session, or invitation credentials.

`NetConnection` owns the shared two-byte big-endian frame prefix and message
codecs. `NetTransport` implementations exchange bounded byte chunks; WebSocket
messages may split or combine game frames. Inbound/outbound transport buffering
is capped at 1 MiB, and the decoded-message queue at 256 messages. Oversized,
empty, unknown, truncated, or trailing-data messages close the connection. The
initial greeting is retained while the asynchronous connection opens.

Run `scons release=1 transport-test`, then the `net-connection-test` executable
with an unused local TCP port to cover framing, rejected packets, queue limits,
outbound limits, credential-log redaction, and a real TCP round trip. The Playwright multiplayer test uses that executable's
isolated YOG fixture and the actual gateway, with real login controls and observed
wire messages. It also enters and leaves the lobby with an isolated fixture
account and saves a lobby screenshot. Match tests create a room through the
actual controls, join from a second browser, ready both players, and compare
checksums from their outgoing orders. A native headless peer also joins through
YOG and records 250 simulation ticks; its checksums are compared with the browser
at the negotiated command cadence. This uses the native game implementation,
not a second simulation model.

Run `cd browser && npx playwright test multiplayer.spec.js` for the cross-browser
multiplayer suite. The default browser/browser cases use no AI and Cortex;
`GLOB2_ALL_AIS=1` covers all six shipped AIs and is enabled nightly. Native
cross-play currently tests a two-human match on the build host. These short
matches do not qualify sustained platform parity, account migration, or recovery.

Native TCP currently runs SDL networking on a worker. SDL's connect/send calls
still need bounded cancellation/deadline handling before release qualification;
native WSS is described below. The browser transport is
callback driven and does not create a worker thread or use Asyncify itself.

## Native secure gateway connections

Desktop client builds now require OpenSSL development headers/libraries alongside
Boost. Headless lobby/router builds and Emscripten do not use this dependency.
Set `GLOB2_YOG_URL=wss://games.example.org` when launching the desktop client to
use that gateway for login, registration, and matches. Supply an origin only,
with an optional port; paths, query strings, and embedded credentials are rejected.
The native client uses `/yog` and `/router` and keeps match traffic on the same
configured gateway even when legacy YOG packets advertise a private router IP.

The WSS transport pumps asynchronous Beast/Asio operations from the application
thread. It verifies the certificate chain and hostname, supplies SNI, and requires
TLS 1.2 or later. OpenSSL's default trust paths are used; `SSL_CERT_FILE` can select
an explicit CA bundle for a private deployment. There is no skip-verification
switch. Qualification of native OS trust-store packaging, especially Windows,
remains required before release.

Connection establishment and writes have ten-second deadlines; WebSocket idle
checking uses a thirty-second timeout with keepalive. Outbound/inbound payload
queues are bounded by 1 MiB, incoming messages by 64 KiB, and incoming queued
messages by 256. Outbound WebSocket chunks are at most 16 KiB. Closing cancels
socket operations; stalled TLS cancellation is tested. System DNS resolver
cancellation and the older SDL TCP worker still need platform-wide qualification.
The legacy default YOG endpoint has not yet been migrated to a TLS-only connection
policy; explicitly configure WSS for the secure self-hosted path.

Run `scons release=1 transport-test`, then
`python3 -m unittest discover -s tests/transport -v` for actual TLS peers covering
trusted echo, fixed routes, untrusted certificates, hostname mismatch, text and
oversized frames, rejected credential/path URLs, cancellation, and timeout.
Playwright's native cross-play cases run with both TCP and WSS native peers. The
WSS case uses an isolated test CA and TLS terminator in front of the real gateway;
production proxy routing is covered separately by the Compose suite.
