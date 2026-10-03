# Secure network transports

Desktop, mobile and browser clients reach the network only over TLS: HTTPS for
the platform's REST API (`HttpFetch`), and secure WebSockets for the platform's
realtime channel, the match relay and LAN hosts. `NetTransport`
(`src/net/NetTransport.h`) is the one WebSocket abstraction. Natively,
`WssTransport` implements it with Boost.Beast; in the browser,
`browser/NetTransport.cpp` uses the page's WebSocket. There is no
WebSocket-to-TCP gateway and no raw-TCP fallback for public traffic.

| Connection | Endpoint | Mode | Guide |
| --- | --- | --- | --- |
| Platform realtime | `wss://<instance>/realtime` | Text (JSON) | [Client](../multiplayer/client.md) |
| Match relay | `wss://<instance>/relay/<relay id>` (from the match ticket) | Binary (turn protocol) | [Relay](../multiplayer/relay.md), [turn protocol](../multiplayer/turn-protocol.md) |
| LAN host | `wss://<address>:7489/yog#sha256=<fingerprint>` (the pairing string) | Binary (room and turn protocol) | [LAN](../multiplayer/lan.md) |

Endpoints are explicit URLs. The URL determines both port and route, and
credentials and query parameters are refused. The LAN route is still `/yog`: it
is only the path the host's listener answers on, kept so pairing strings stay
compatible, and has nothing to do with the YOG lobby, which was removed at the
M9 cutover.

## Transport guarantees

WebSocket binary messages carry chunks of the length-prefixed `NetConnection`
stream. An application frame can span WebSocket messages, and one WebSocket
message can contain several frames. WebSocket messages are limited to 64 KiB;
outgoing chunks are at most 16 KiB. Each direction has a 1 MiB queue limit, and
incoming chunks are also limited to 256. Text messages on a binary connection
and oversized input fail closed.

A connection can instead be opened (`makeNetTransport(tls, NetMessageMode::Text)`)
or accepted (`NetListenConfig::messageMode`) in text mode, for JSON protocols:
`sendText`/`receiveText` exchange whole UTF-8 text messages of at most 256 KiB,
binary messages close the connection, and the byte-stream calls are refused. The
same queue limits apply. Native peers reject invalid UTF-8 (Beast validates text
frames); the browser decodes text frames itself. Messages must not contain NUL.

Native transport uses asynchronous Boost.Beast/Asio I/O, pumped by the owning
application thread. Connection establishment and writes have ten-second
deadlines; established sockets use idle timeouts and WebSocket keepalive pings.
Closing cancels pending socket operations. Listener handshakes are bounded and
include pending TLS connections in the connection limit. Name resolution still
depends on the platform resolver.

Native client and relay builds require OpenSSL and Boost.Beast/Asio; `wss=0` is
rejected. TLS requires version 1.2 or later; clients verify certificate
chains and DNS/IP identity. An explicit test/private CA can be configured with
`SSL_CERT_FILE`. macOS/iOS use Security trust evaluation; Windows uses its
certificate-chain SSL policy; Android uses its platform trust manager, and Linux
uses the distribution CA store. Browser trust belongs to the browser/device.
There is no production skip-verification mode or plaintext fallback.

## Offline LAN

A native host generates an in-memory, seven-day TLS identity for its hosting
session. The room screen shows a pairing string and a copy button. The string
contains the host's WSS endpoint and the full SHA-256 certificate fingerprint,
for example `wss://192.168.1.20:7489/yog#sha256=...` (64 hex digits). Guests
obtain this string directly from the host and select **Pair and connect**. The
fingerprint is not a password; it identifies the certificate guests trust for
this session. New sessions require new pairing. The certificate must also be
current and identify the endpoint's DNS name or IP address.

Discovery is the deliberate plaintext exception: a bounded `G2D1` UDP beacon
advertises only an opaque session identifier and WSS endpoint. No game name,
player list, credentials or gameplay data is broadcast. The LAN browser lists
unverified hosts generically; the room arrives over verified WSS after pairing.
Discovery never establishes trust. Manual pairing entry works when broadcasts are
unavailable. `GLOB2_LAN_ADDRESS` selects a numeric host address when automatic
interface selection chooses the wrong network. Certificates include local
interface addresses at session creation; restart the hosting session after
network/interface changes.

Browser guests can use LAN certificates already trusted by their device;
JavaScript cannot override TLS validation with an application fingerprint. Set
`GLOB2_ALLOWED_ORIGINS` on the native LAN host to the exact HTTPS browser
application origin (or a loopback development origin); an empty allowlist
refuses browser Origins. Origin checks are not authentication. Browser hosting
is not supported.

On iOS, local-network permission and Apple's multicast entitlement are needed
for UDP broadcast discovery. Denied/unavailable discovery permits manual entry;
denied connections display a network/identity error. Android currently targets
API 36, where INTERNET covers LAN access; targeting API 37 will require a
separate ACCESS_LOCAL_NETWORK permission flow.

## Verification

```sh
scons release=1 transport-test -j4
python3 test/run-network-transport-tests.py
python3 -m unittest discover -s tests/transport -v
```

`net-connection-test` covers `NetConnection` framing, malformed input and queue
limits on a fake transport, then a native WSS round trip, a TCP listener and
transport backpressure on loopback. `tests/transport/` drives the WSS client and
listener harnesses with real certificates: verified clients/listeners, explicit
routes, certificate pinning, mutual TLS, Origin checks, binary framing, text
mode, rejection, backpressure and stalled TLS cancellation.
`tests/transport/test_http_fetch.py` drives the native `HttpFetch` client
(`src/online/HttpFetch.h`; `emscripten_fetch` in the browser) against local HTTP
and HTTPS servers: methods, headers and bodies, error statuses, certificate
trust, response limits, timeouts and cancellation. It shares WssTransport's TLS
trust through `src/net/TlsSetup.h`.

The relay's own listener and admission are covered by `glob2-relay-tests` and
`tests/relay/` ([relay](../multiplayer/relay.md)), and the platform stack by
`tests/deployment/platform_stack_smoke.py` ([hosting](../hosting/README.md)).
