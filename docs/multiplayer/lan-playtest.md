# LAN playtest guide

LAN games now run on the relay-sequenced [turn protocol](turn-protocol.md), with the
relay inside the host's game ([how LAN works](lan.md)). This is the first playtest of
the new netcode's feel. The screens look as before; what changes is how input, lag and
lost connections behave.

## What to evaluate

1. **Input delay.** Place flags and buildings and change worker counts, on the host and
   on a guest. How long the game takes to react, compared with the old LAN game, is the
   main question. Use the numbers below as a baseline.
2. **One slow player no longer slows everyone.** Put one guest on Wi-Fi or behind an
   emulated slow link (see below). That guest should get more input delay; the others
   should not.
3. **Catch-up.** Pause a guest's process for a few seconds (`kill -STOP <pid>`, then
   `kill -CONT <pid>`, or drag its window on Windows). The others keep playing and show
   "<name> is lagging". The paused guest then runs fast with few frames drawn, showing
   "Catching up: N turns behind", and continues normally.
4. **Reconnect.** Unplug a guest's network cable or turn Wi-Fi off for 10–60 s. The guest
   shows "Connection to the host lost. Reconnecting..." and the others show "<name> is
   reconnecting...". After reconnecting, the guest catches up. Its colony kept working
   meanwhile.
5. **Rejoin after a crash.** Kill a guest's process during play, start it again, and join
   the same host under the same player name. The guest should rejoin its seat and
   fast-forward from the start of the game.
6. **The host leaving.** Quit on the host. Every guest's game ends with "The host left
   the game." instead of hanging.
7. **Anything that feels different:** pacing, smoothness (each player's speed is nudged
   by up to 5% to hold its buffer), and room behaviour (joining, teams, readiness, chat,
   map download, Other Options).

## Running two instances

Build the client (`scons -j3 release=1 server=0`, see the
[development reference](../development/README.md)). The binary is
`build/<platform>/client/release/src/glob2`; run it from the repository root.

**On one machine.** Give each instance its own profile, so settings, map caches and
replays stay apart:

```sh
GLOB2_USER_DIR=/tmp/glob2-host  GLOB2_LAN_ADDRESS=127.0.0.1 build/darwin/client/release/src/glob2
GLOB2_USER_DIR=/tmp/glob2-guest build/darwin/client/release/src/glob2
```

1. Host: **LAN Game → Host**, choose a map. The room shows the pairing string and a
   **Copy pairing string** button.
2. Guest: **LAN Game → Join a game**, paste the pairing string into the host field, and
   **Pair and connect**. The host appears in the list, but the pairing string is still
   needed to verify it.
3. Guest: wait for the map download, tick **Ready**. Host: add AIs if wanted, then
   **Start**.

**On two machines.** Run one instance on each machine. `GLOB2_LAN_ADDRESS` is only
needed if the host picks the wrong network interface; set it to the host's LAN IP. The
host listens on TCP port 7489, and discovery uses UDP broadcasts, so allow both through
the host's firewall. On macOS, allow the "local network" prompt.

**Slow-link emulation (optional).** On macOS, use the Network Link Conditioner (Xcode
additional tools) on the guest. On Linux, run `sudo tc qdisc add dev <iface> root netem
delay 50ms 20ms` on the guest, and remove it afterwards with `sudo tc qdisc del dev
<iface> root`.

## Collecting evidence

Start every instance with a checksum sidecar and a replay path:

```sh
GLOB2_CHECKSUM_SIDECAR=1 GLOB2_REPLAY_PATH=/tmp/lan-host.replay GLOB2_USER_DIR=/tmp/glob2-host \
  GLOB2_LAN_ADDRESS=127.0.0.1 build/darwin/client/release/src/glob2
GLOB2_CHECKSUM_SIDECAR=1 GLOB2_REPLAY_PATH=/tmp/lan-guest.replay GLOB2_USER_DIR=/tmp/glob2-guest \
  build/darwin/client/release/src/glob2
```

After the game, check that the players agree at every tick they both played:

```sh
python3 test/run_lan_session_test.py - --compare /tmp/lan-host.replay.checksums /tmp/lan-guest.replay.checksums
```

A guest that reloaded during the game (after a desync or rejoin) starts its replay and
sidecar again from tick 0, so compare its final run. The host also writes the relay's
match record to `<host user dir>/replays/lan-last.g2mr`, which can be checked against the
map:

```sh
build/darwin/client/release/src/glob2 match verify /tmp/glob2-host/replays/lan-last.g2mr \
  --map maps/<the map>.map.gz --output-dir /tmp/lan-verify
```

`/tmp/lan-verify/verdict.json` should say `verified`. Attach the sidecars (or the compare
output), the replays, the match record and the verdict to the playtest report, with
notes on what felt different and when (game minute, which player).

## Baseline input delay

Run `LanMatchHarness`'s `LAN input delay on loopback and delayed links` benchmark:

```sh
python3 test/run_tests.py --binary engine --tag benchmark --filter 'LanMatchHarness/*'
```

A host and guest play FourSquares1 with a Nicowar AI over loopback WSS. The case
varies the guest's one-way delay and bundle interval, queues GUI orders between
frames, and writes latency distributions, stage breakdowns and stall counts.
Record the revision, build flags, OS/architecture, host load and generated reports
for each comparison. Use the same inputs when evaluating a pacing or buffering
change; do not use an old machine's measurements as universal acceptance limits.

The host's latency should not grow with the guest's link delay, and a slow guest
should not hold up the host's simulation. Assess responsiveness and short Wi-Fi
hitches while playing, alongside the measured pickup, transport and buffer waits.
The [timing contract](turn-timing.md#measured-delay) explains measurement boundaries.


[Multiplayer index](README.md) · [Documentation index](../README.md).
