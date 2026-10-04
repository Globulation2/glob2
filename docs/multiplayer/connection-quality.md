# Connection quality: what players see

Players see three connection quantities, each with its own name, unit and threshold
table. Every surface uses the same names, units, words and limits: the in-game
connection panel (desktop and phone), the match-start checklist, the quick-match card,
room seats and the match page in the web app. The thresholds come from one table,
`platform/packages/protocol/src/connectionQuality.ts`, and the game has a checked copy
of it. All three quantities are presentation only. Nothing simulated, rated or
verified reads them.

## The quantities

| Name | Meaning | Measured by | Shown |
| --- | --- | --- | --- |
| **Ping** | Round trip between a player and the relay | The relay, per seat (WebSocket ping every `GLOB2_RELAY_RTT_PING_MS`, smoothed with weight ¼), sent to clients in `SeatLatency`. The own row falls back to the client's own `Ping`/`Pong` round trip | Every human row of the panel; details; room seats; match page (typical and 95th percentile) |
| **Delay** | Your input delay: from your click until it happens, for everyone | This client: half its round trip plus the ticks its jitter buffer holds | Panel footer ("Your delay 171 ms · Good"); details |
| **Behind** | How far a player's game runs behind the match clock. It includes their own delay, so under a second is normal | The relay: relay tick minus the tick the player last reported executing (`Presence.lagTicks`) | A row switches from Ping to Behind once it is fair or worse; details; match page |

Before a match there is no relay connection yet. The quick-match card shows the round
trip of the best **region probe** as "Ping about 42 ms · Good". It is only an estimate,
because the matchmaker picks the relay later and the region preference relaxes with
wait time. The match-start checklist shows the session's own round trip to the
assigned relay once it is connected.

A player's colour, name, state word and marker shape always appear next to the
number. "Delay" is never compared with another player's "Ping". The two measure
different things.

## Thresholds

A value at or above *fair* is fair; at or above *poor* it is poor.

| Quantity | Good | Fair | Poor |
| --- | --- | --- | --- |
| Ping | under 150 ms | 150–299 ms | 300 ms and up |
| Delay | under 200 ms | 200–399 ms | 400 ms and up |
| Behind | under 1 s | 1–1.9 s | 2 s and up (the relay's *slow* state starts here) |

Each rating has a word, a colour and a marker shape, so colour is never the only
signal:

| Rating | Word | Colour | Marker |
| --- | --- | --- | --- |
| good | Good | green | filled disc |
| fair | Fair | amber | ring |
| poor | Poor | red | triangle |

Values are written with their unit everywhere: milliseconds as `42 ms`, Behind in
seconds as `1.4 s` (one decimal below 10 s, then whole seconds).

The match page rates each player's *typical* (median) Ping and Behind with the same
table. Its overall **Quality** is the worst of those two ratings and of the
reconnect, offline-time, delayed-order and rejoin rules in
`platform/apps/api/src/history/network.ts`.

## In-game panel

`src/net/ConnectionOverlay.*` reads a `ConnectionSnapshot` that `TurnMatchPresenter` (`src/net/turn/TurnMatchPresenter.*`) builds from the read-only `TurnSession`.

- **Rows:** colour, name ("You" on the own row) and the value: `42 ms · Good`, or
  `Behind 1.3 s · Fair` once a player falls a second behind (or when no Ping is
  known, as with LAN guests). States replace the value: `Reconnecting 2:12`,
  `Rejoining`, `Connecting…`, `Left`, `AI`. A player the relay marks slow is rated
  poor.
- **Footer:** `Your delay 171 ms · Good`, plus `· unstable` when this client's jitter
  is above 60 ms.
- **Phones with more than four humans:** a two-column grid of colour, marker and
  number (Ping in ms, or seconds behind), and the delay footer. The details sheet
  explains the grid in a legend.
- **Details (click or tap):** Ping, Behind and state for every player, a
  Good/Fair/Poor legend with the markers, and one sentence each on Delay, Ping and
  Behind with their limits.

## Wire and versions

`SeatLatency` (`0xAC`, turn protocol version 2) carries `u8 count` and then, for each
connected human seat, `u8 seat` and `u32 rttMicros` (0 when not measured). The relay
sends it with every `Presence` broadcast, but only to clients that said version 2 in
`Hello`. A version-1 client never receives it. Relays accept versions 1 and 2 and
answer `Welcome` in the client's version. A version-2 client that an older relay
refuses with `Reject(1)` retries once with version 1 and then plays without other
players' Ping. Replays, match records, saves and the simulation are unchanged
([turn protocol](turn-protocol.md#framing-and-versioning)).

`Presence.lagTicks` is the lag the seat's last `Ping` reported, and no longer grows
between pings. Clients ping every 500 ms. Once a second passes without a ping, the
extra time counts as lag, so a silent client still turns *slow* after two seconds.

## Changing a threshold

1. Edit `CONNECTION_METRICS` in `platform/packages/protocol/src/connectionQuality.ts`
   and run `npm run fixtures` in `platform/`. This regenerates
   `fixtures/connection-quality.json` with its sample cases.
2. Copy that file to `test/fixtures/protocol/connection-quality.json` and update
   `limits()` in `src/net/ConnectionQuality.h`.
3. `glob2-unit-tests -ts="connection quality"` checks the C++ table and every sample
   case against the JSON. `packages/protocol/test/connectionQuality.test.ts` checks the
   TypeScript side. The match page's legend text is generated from the table.

Changing a threshold changes how connections are labelled and how the game feels to
read. Call it out in the pull request.
