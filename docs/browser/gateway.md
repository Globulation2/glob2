# Development WebSocket gateway

This is transport infrastructure, not yet a supported multiplayer release.
The browser YOG entry now uses WebSocket transport and the same message codecs
as native TCP clients. The legacy YOG handshake, successful account login, and lobby exit are exercised
against a native server through the gateway. Browser chat stays within YOG;
the optional native IRC bridge is disabled. Upgraded protocol negotiation, native
WSS, account migration, invitation rooms and coordinated recovery remain release
gates; complete cross-play matches are not yet certified.

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
routing. A supported Compose distribution has not yet been delivered.

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
multiplayer suite. The default browser/browser cases use no AI and Maxima;
`GLOB2_ALL_AIS=1` covers all seven shipped AIs and is enabled nightly. Native
cross-play currently tests a two-human match on the build host. These short
matches do not qualify sustained platform parity, account migration, or recovery.

Native TCP currently runs SDL networking on a worker. SDL's connect/send calls
still need bounded cancellation/deadline handling before release qualification;
native secure WebSocket transport remains outstanding. The browser transport is
callback driven and does not create a worker thread or use Asyncify itself.
