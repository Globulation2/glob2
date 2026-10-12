# Network verification

Focused regression scenarios and commands. Start with the [native test guide](../../../test/README.md) for building, isolation and runner selection.

## Match relay

`scons role=relay release=1 relay` builds `glob2-relay` and `glob2-relay-tests`, a
doctest binary of its own (the relay role builds no engine or SDL code). Run it
directly, then `python3 -m unittest discover -s test/relay_service -v` for the end-to-end
tests against the real binary. `test/fixtures/relay-tickets/` copies the protocol
package's ticket fixtures. See [docs/multiplayer/relay.md](../../multiplayer/relay.md#tests).
## Online screens and test switches

Release builds have no switch that opens an online screen directly; players reach
them through the online hub. To look at a screen offline, render its canned states:
`src/ui/OnlineUIFixtures.h` holds the fixtures (hub, room, match start, quick match,
profile, maps) for the `UIPresentation` cases and `scons release=1 mobile-gallery`.
`PlatformClientTest` in the unit binary covers the client's request lifetimes,
including screens destroyed with requests in flight.

Switches the online and LAN tests use:

- `--instance <origin>`: the instance an invite code given with `online join CODE`
  belongs to; a `glob2://` or `https://<instance>/j/<code>` argument works too.
- `online turn-client`, `match verify`, `info sim-version --format json`: headless relay client, match
  verifier and sim version report ([headless replays](../headless-replays.md)).
- `GLOB2_LAN_ADDRESS=<ip>`: the address a LAN host advertises and puts in its
  certificate, for machines with several interfaces.
- `GLOB2_LAN_DELAY_BUNDLE=1`: only the one-tick-bundle rows of the LAN input delay
  benchmark in [LAN session verification](#real-lan-session-regression).

## Real LAN session regression

The direct transport/security checks use `scons release=1 transport-test`,
`python3 test/run-network-transport-tests.py`,
`build/darwin/client/release/src/lan-discovery-test` (substitute your platform),
and `python3 -m unittest discover -s test/transport -v`. Deployment script
checks use `python3 -m unittest discover -s test/deployment -v`; the whole
platform stack is exercised by `test/deployment/platform_stack_smoke.py` (see
`docs/hosting/README.md`). Keep capture output from
`test/transport/capture_container.py` under ignored `artifacts/`.

The online client's integration test, `test/online_service/test_platform_client.py`,
drives `platform-client-probe` (also built by `transport-test`) against a real
platform API; it needs a `platform/` checkout and a Postgres role that may
create databases, and is skipped otherwise (see `docs/multiplayer/client.md`).

From the repository root:

```sh
scons --build=build/native-tests -j2 release=1 lan-test
python3 test/run_lan_session_test.py build/native-tests/src/LANSessionHarness
```

This runs separate host and joining client processes with real SDL lobby widgets, a
`LanRoom` host (room and in-process turn relay) and paired WSS connections. The joiner
uses the actual `LANFindScreen` Pair and connect path with the host session fingerprint.
It clicks Ready and Leave Game, then rejoins. Both cycles download the map into the
guest's content-addressed map cache and compare its bytes with the fixture source
(`maps/FourSquares1.map`). The host verifies readiness, roster size, unique names and
seat numbering, and both departures. Linux CI runs this automatically with SDL's dummy
video/audio drivers.

`--play SECONDS` plays a real game instead: the host presses Start in the room, both
processes run their `GameSessionScreen`s, the host quits after SECONDS, and the guest's
game must end with "host left". The two processes' per-tick checksum sidecars must agree
on every tick both executed:

```sh
python3 test/run_lan_session_test.py build/native-tests/src/LANSessionHarness --play 30
```

`LanMatchHarness` in the engine binary covers the match itself in one process: a host
and two guests over loopback WSS with real engines, a dropped connection, a guest that
restarts and rejoins by name, the host leaving, identical per-tick checksums and a
verified match record. Its `[benchmark]` case measures input delay, per stage and with
stall counts (`docs/multiplayer/lan-playtest.md`; `GLOB2_LAN_DELAY_BUNDLE=1` runs only
the one-tick-bundle rows). `TurnHarness` (unit binary) and `TurnEngineHarness` (engine
binary) have `[benchmark]` cases that measure the same on the simulated network, per
link profile (`docs/multiplayer/turn-timing.md#measured-delay`).

`OnlinePlayHarness` (`scons release=1 server=0 online-play-test`) plays an online
room through the real hub, Room, starting and results screens against a live
instance in a host and a guest process; see "End-to-end check" in
[docs/multiplayer/client.md](../../multiplayer/client.md).

For two physical machines, run these from each machine's repository root, using
absolute capture prefixes whose parent directories already exist:

```sh
SDL_VIDEODRIVER=dummy ./build/native-tests/src/LANSessionHarness host 127.0.0.1 2 /tmp/lan-host
SDL_VIDEODRIVER=dummy ./build/native-tests/src/LANSessionHarness join 'HOST_PAIRING_STRING' 2 /tmp/lan-guest
```

Start the joiner after the host prints `HOST roster=1`, copying its full
`PAIRING` string. The host's TLS/WebSocket TCP port 7489 must be reachable;
nothing connects to a public service. Omit
`SDL_VIDEODRIVER=dummy` to show the real window. Normal game profiles are preserved;
the harness uses `.glob2-lan-test-host` and `.glob2-lan-test-join` profiles containing
only test data. Fixed input timers allow map transfer before leaving; the runner
bounds startup, execution, and child cleanup. Logs and captures are written under
`artifacts/lan-session-test` by default (`--output` overrides it).
