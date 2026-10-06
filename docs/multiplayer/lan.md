# LAN games on the turn protocol

LAN games use the same relay-sequenced turn protocol as online matches
([turn protocol](turn-protocol.md)). The host's own game process runs the room and the
relay core (`TurnSequencer`) in-process; no server or router is involved. For
how to try it out, see the [LAN playtest guide](lan-playtest.md).

## Pieces

| Part | Code | Role |
| --- | --- | --- |
| Room backend | `src/online/RoomBackend.h` | What the room screen needs from the network: seats, teams, readiness, chat, map progress, options, start, and starting the engine |
| LAN room | `src/net/lan/LanRoom.*` | `RoomBackend` for LAN: wraps a `LanHost` or a `LanClient` |
| Host | `src/net/lan/LanHost.*` | Listener, discovery beacon, room state, map serving, and the `TurnSequencer` once the match starts |
| Guest | `src/net/lan/LanClient.*` | Joins the room, downloads the map, and hands the engine a turn transport that shares the room connection |
| Wire format | `src/net/lan/LanProtocol.*` | Framing (`LanLink`), room messages, `RoomState` |
| Map cache | `src/online/MapCache.*` | The online client's content-addressed cache: `<hash>.map.gz` / `<hash>.game.gz` under `<user dir>/online/maps` |

`LANMenuScreen` hosts (after `ChooseMapScreen`), `LANFindScreen` discovers hosts and
takes the pairing string, and `LANSessionScreen` shows progress and owns the room,
which is the shared `RoomScreen` on the `LanRoom` backend.

## Connection and discovery

The host listens on a pinned WSS endpoint (the `/yog` path is kept from the former
YOG LAN server so pairing strings stay compatible):
`wss://<address>:7489/yog#sha256=<fingerprint>`, with a session-only certificate from
`provisionLanIdentity`. `GLOB2_LAN_ADDRESS` picks the advertised address. Discovery is
unchanged: `NetBroadcaster` announces the endpoint without the fingerprint, so a guest
still pastes the host's pairing string to verify it.

Each guest keeps one connection to the host. It carries `NetConnection` framing (a
2-byte big-endian length, then the payload, whose first byte is the message type).
Turn messages use `0xA0`–`0xBF`; the room uses `0xC0`–`0xCF`, reserved in
`NetMessageType.h`. `LanLink` frames without `NetConnection`'s 256-message receive cap,
because a rejoining guest receives the whole turn log at once.

The host services its connections and the relay on its own thread. Guests are therefore
served at the relay's pace whatever the host's own engine or UI is doing. The host's own
player reaches the relay through an in-memory transport.

The thread does not poll on a timer. It blocks (`netWait` in `src/net/NetWait.h`) until one
of these happens:

- a guest connection or the listening socket is ready (`NetTransport::waitHandles`);
- the relay's next bundle or timer is due (`TurnSequencer::nextWakeMicros`);
- another thread queued work for it, such as the host's own player or a room action;
- 100 ms pass, which bounds the transports' own handshake, write and ping timers.

A running match therefore wakes it about once per tick rather than 1000 times a second.
Windows sockets complete through I/O completion ports, which a readiness poll cannot see,
so on Windows (and in the browser) the thread still polls every millisecond.

## Room messages

`0xC0 RoomJson` is a `u32` length, then a UTF-8 JSON object with a `type` member, at most
32 MiB. Messages larger than one 65,535-byte frame use `0xC2 JsonChunk`: a `u32`
total payload size, a `u32` offset, then up to 48 KiB of the original RoomJson payload
(including its type and length). Chunks must be contiguous, ordered, and cannot
interleave other frames. Receivers reject gaps, overlaps, mismatched totals, and
transfers exceeding 32 MiB plus the five-byte RoomJson header. This carries the
embedded building catalog without changing the turn-message frame size.

`0xC1 MapChunk` carries the 32-byte SHA-256 of the decompressed map, the
`u32` offset and `u32` total size of the gzip-compressed transfer, then up to 48 KiB.

| Type | Direction | Members |
| --- | --- | --- |
| `hello` | guest → host | `protocol` (room protocol, 2), `turnProtocol`, `simVersion` (key), `name` |
| `welcome` | host → guest | `member`, `name` (made unique) |
| `refuse` | host → guest | `reason`: `version`, `full`, `started`, `kicked`, `malformed`; `detail` |
| `state` | host → all | `setup` (MatchSetup, seed 0 until the start), `mapName`, `hostName`, `teamColors`, `mapBytes`, `members` (`id`, `name`, `seat`, `ready`, `hasMap`), `started` |
| `setTeam` | guest → host | `seat` (its own), `team` |
| `ready` | guest → host | `ready` (accepted only once the guest has the map) |
| `mapRequest` | guest → host | `offset`; the guest keeps 8 chunks in flight |
| `mapReady` | guest → host | `hash` |
| `chat` | both | `text` (guest → host); `from`, `text` (host → all) |
| `start` | host → guest | `setup` (with the seed), `seat`, `ticket`, `mapName`, `mapBytes` |
| `leave` | guest → host | |
| `closed` | host → all | `reason`: `host-left` or `cancelled` |

The room state is the `MatchSetup` the match will start from, so starting needs no
conversion: every player calls `Engine::initTurnMatchTask` with the setup, its seat and
its ticket. The map source is `upload` (format `map` or `save`). A saved game keeps its
rules, alliances and experiments; a map takes the host's experiment settings, as the
legacy lobby did. The options screen edits a `GameHeader` built with `toGameHeader`, and
the host applies it back with `MatchSetup::fromGameHeader`.

A guest that sends a different room protocol, turn protocol or sim version is refused
with `version`.

## The match

- **Tickets.** At the start the host draws a random 128-bit ticket per human seat; the
  relay's admission function maps tickets to seats. LAN needs no signed tickets.
- **Synchronized start.** The relay's clock starts once every human seat's first `Hello`
  has arrived, so no player starts behind while the others load (or after 30 s).
- **Bundles.** The LAN relay sends a bundle every tick (`bundleInterval` 1), as online
  relays do by default.
- **Reconnect.** A guest whose connection drops reconnects to the same pinned endpoint by
  itself and resumes from its horizon. A guest that hears nothing from the host for 5 s
  treats the connection as lost. After 2 minutes without reaching the host, the guest's
  game ends.
- **Rejoin after a restart.** A guest whose game restarted joins the room again under the
  same name. While its seat is reconnecting or not yet connected, the host sends `start`
  again with the seat's ticket, and the guest fast-forwards from tick 0.
- **Host leaving.** When the host's game ends (or the host quits), the relay finishes
  (final bundle) and every connection gets `closed` with `host-left`. Shutdown drains
  both the LAN outbox and pending transport writes, then closes the drained send side
  before releasing the socket so Windows preserves the final notice. Each guest's
  transport then delivers `Reject(MatchOver)`, which ends its session cleanly, and the
  room shows "The host left the game."
- **Record.** The host writes the match record to `<user dir>/replays/lan-last.g2mr`;
  `glob2 --verify-match` replays it. Next to it, `lan-last.network.json` holds the
  relay's per-seat network summary ([network telemetry](../development/network-telemetry.md)).

## In-game connection notice

Turn games replace the "waiting for X" box (`GameGUIDraw.cpp`) with connection lines
from `TurnMatchPresenter::notice`, whenever there is something to report: the local
connection being lost or everyone loading, a rejoin or catch-up of more than 25 turns,
and other players who are reconnecting, lagging, catching up or not yet connected. The
always-on connection panel of the multiplayer revamp will replace this box.
