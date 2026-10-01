# Secure multiplayer transports

Desktop, mobile, and browser clients use binary secure WebSockets directly with
YOG lobby and game-router listeners. LAN hosts run the same listeners. The
WebSocket-to-TCP gateway and native raw-TCP fallback have been removed.

## Endpoints and compatibility

Endpoints are explicit URLs: `wss://host:port/yog`, `wss://host:port/router`, and
private `wss://host:port/register`. The URL determines both port and route;
legacy YOG port arguments no longer select routes. Credentials and query
parameters are forbidden. The default public endpoint is
`wss://yog.globulation2.org/yog`; the public service must be upgraded and
qualified before releasing clients with this transport cutover. Use
`GLOB2_YOG_URL=wss://your-host/yog` for a separate native deployment.

Browser deployments default to the page's host over WSS. A separate lobby is
configured before starting the application:

```javascript
globalThis.glob2Config = {yogEndpoint: 'wss://games.example.org/yog'};
```

The authenticated lobby supplies the complete router URL. Browser clients use
that URL rather than forcing every match through their initial lobby origin.
Each public listener accepts native clients without Origin and checks supplied
browser Origins against `GLOB2_ALLOWED_ORIGINS`. Origin checks supplement the
existing YOG login and game protocol; they are not authentication.

Network protocol 49 requires these endpoints and versioned router registration.
Older clients and raw-TCP servers need a coordinated upgrade. Save/map format
124, minimum save version 58, and replay acceptance remain unchanged. Existing
accounts and maps retain their storage format. Native lobby chat stays on YOG;
the optional plaintext IRC connection is disabled.

## Transport guarantees

WebSocket binary messages carry chunks of the existing length-prefixed protocol
stream. An application frame can span WebSocket messages, and one WebSocket
message can contain multiple frames. There is no JSON conversion or simulation
change. WebSocket messages are limited to 64 KiB; outgoing chunks are at most
16 KiB. Each direction has a 1 MiB queue limit and incoming chunks are also
limited to 256. Text messages and oversized input fail closed.

Native transport uses asynchronous Boost.Beast/Asio I/O, pumped by the owning
application thread. Connection establishment and writes have ten-second
deadlines; established sockets use idle timeouts and WebSocket keepalive pings.
Closing cancels pending socket operations. Listener handshakes are bounded and
include pending TLS connections in the connection limit. Name resolution still
depends on the platform resolver.

Native builds, including headless server/router roles, require OpenSSL and
Boost.Beast/Asio. `wss=0` is rejected. TLS requires version 1.2 or later; public
clients verify certificate chains and DNS/IP identity. An explicit test/private
CA can be configured with `SSL_CERT_FILE`. macOS/iOS use Security trust
evaluation; Windows uses its certificate-chain SSL policy; Android uses its
platform trust manager, and Linux uses the distribution CA store. Browser trust belongs to the
browser/device. There is no production skip-verification mode or plaintext
fallback.

## Offline LAN

A native host generates an in-memory, seven-day TLS identity for its hosting
session. The waiting-room screen shows a pairing string and a copy button. The
string contains the lobby WSS endpoint and the full SHA-256 certificate
fingerprint, for example `wss://192.168.1.20:7489/yog#sha256=...` (64 hex digits).
Guests obtain this string directly from the host and select **Pair and connect**.
The fingerprint is not a password; it identifies the certificate guests trust
for this session. New sessions require new pairing. The certificate must also
be current and identify the endpoint's DNS name or IP address.

Discovery is the deliberate plaintext exception: a bounded `G2D1` UDP beacon
advertises only an opaque session identifier and WSS endpoint. No game name,
player list, credentials, or gameplay data is broadcast. The LAN browser lists
unverified hosts generically; game information arrives over verified WSS after
pairing and anonymous LAN login. Discovery never establishes trust. Manual
pairing entry works when broadcasts are unavailable. `GLOB2_LAN_ADDRESS` selects
a numeric host address when automatic interface selection chooses the wrong
network. Certificates include local interface addresses at session creation;
restart the hosting session after network/interface changes.

Native LAN lobby/router registration uses WSS with locally provisioned mutual
trust. Browser guests can use LAN certificates already trusted by their device;
JavaScript cannot override TLS validation with an application fingerprint.
Set `GLOB2_ALLOWED_ORIGINS` on the native LAN host to the exact HTTPS browser
application origin (or a loopback development origin). It applies to the LAN
lobby and gameplay listeners as well; an empty allowlist refuses browser Origins.
Browser hosting and offline certificate provisioning are not supplied.

On iOS, local-network permission and Apple's multicast entitlement are needed
for UDP broadcast discovery. Denied/unavailable discovery permits manual entry;
denied connections display a network/identity error. Android currently targets
API 36, where INTERNET covers LAN access; targeting API 37 will require a
separate ACCESS_LOCAL_NETWORK permission flow.

## Verification and hosting

```sh
scons release=1 transport-test -j4
python3 test/run-network-transport-tests.py
python3 -m unittest discover -s tests/transport -v
```

These tests exercise verified native clients/listeners, explicit routes,
certificate pinning, mutual TLS, Origin checks, binary framing, rejection,
backpressure, and stalled TLS cancellation. Browser multiplayer tests use native
WSS lobby/router fixtures directly, with isolated test certificates. Their test
browser trust exception is confined to automation.

The [Docker deployment](../../deploy/README.md) provides public certificate
management, verified WSS upstreams, private control endpoints, and draining.
Neither a proxy nor a router simulates a match. Losing a client connection still
ends that player's participation; restart recovery is future work.
