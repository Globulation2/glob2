# Development WebSocket gateway

This is transport infrastructure, not yet a supported multiplayer release.
The browser client still has multiplayer disabled. Protocol negotiation, native
WSS, browser transport integration, account migration, invitation rooms and
coordinated recovery remain separate delivery gates.

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
