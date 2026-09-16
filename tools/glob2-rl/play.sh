#!/bin/bash
# Play Glob2 with a live, network-driven Neurotica.
#
# Neurotica is already selectable in the GUI (see AINames.h), but with no policy
# server it falls back to an inert identity field and does nothing at all. This
# supplies the missing half: it starts a policy server on therig's GPU, forwards
# its Unix socket here over SSH, and launches the game pointed at that socket.
#
# Inference runs on therig, so nothing needs installing locally. One policy step
# per 25 ticks per Neurotica team is roughly one request per second at normal
# speed, so LAN latency is irrelevant.
#
# Two traps this script exists to avoid, both of which fail silently:
#
#   * The server must run with --top-k (or --sample). A per-cell argmax over the
#     building head is empty in practice -- "no building" wins almost everywhere
#     because buildings are ~0.1% of cells -- so the desired field comes out
#     blank and the AI sits on its starting base for the whole game.
#   * The tunnel must bypass SSH connection multiplexing. With an auto-mux
#     master active, `ssh -N -L` hands the forward to the master and exits
#     immediately, so the socket disappears and the AI silently gets no field.
#
# Usage:
#   play.sh                       # launch the game with Neurotica available
#   CKPT=~/neurotica/ppo/policy.pt play.sh    # use the live self-play policy
#   play.sh -test-games --map foo --matchup neurotica,nicowar   # args pass through
set -eu
CKPT=${CKPT:-'$HOME/neurotica/ckpt_full/best.pt'}
RIG=${RIG:-therig.local}
PLACEMENTS=${PLACEMENTS:-12}
SOCK=/tmp/neurotica_gui.sock
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
GLOB2=$ROOT/build/src/glob2

[ -x "$GLOB2" ] || { echo "no glob2 binary at $GLOB2 -- run scons first" >&2; exit 1; }

TUNNEL_PID=""
cleanup() {
  [ -n "$TUNNEL_PID" ] && kill "$TUNNEL_PID" 2>/dev/null || true
  ssh -o ConnectTimeout=10 "$RIG" \
    'for p in $(pgrep -f "neurotica_serve.py.*neurotica_gui"); do [ "$p" != "$$" ] && kill "$p" 2>/dev/null; done' \
    >/dev/null 2>&1 || true
  rm -f "$SOCK"
}
trap cleanup EXIT

echo "starting policy server on $RIG..."
ssh -o ConnectTimeout=20 "$RIG" "
  for p in \$(pgrep -f 'neurotica_serve.py.*neurotica_gui'); do [ \"\$p\" != \"\$\$\" ] && kill \"\$p\" 2>/dev/null; done
  sleep 1; rm -f $SOCK
  CUDA_VISIBLE_DEVICES=1 setsid nohup \$HOME/neurotica/.venv/bin/python \$HOME/neurotica/rl/neurotica_serve.py \
    --checkpoint $CKPT --socket $SOCK --device cuda --max-batch 8 \
    --top-k --placements $PLACEMENTS > /tmp/gui_serve.log 2>&1 < /dev/null &
  sleep 15
  test -S $SOCK" \
  || { echo "policy server failed to start; try: ssh $RIG 'cat /tmp/gui_serve.log'" >&2; exit 1; }

rm -f "$SOCK"
ssh -o ConnectTimeout=20 -o ControlPath=none -o ExitOnForwardFailure=yes \
    -N -L "$SOCK:$SOCK" "$RIG" >/tmp/neurotica_tunnel.log 2>&1 &
TUNNEL_PID=$!
for _ in $(seq 1 15); do [ -S "$SOCK" ] && break; sleep 1; done
[ -S "$SOCK" ] || { echo "socket forward failed; see /tmp/neurotica_tunnel.log" >&2; exit 1; }

cat <<'MSG'

Neurotica is live (inference on therig).

  Custom Game -> set any team's player to AINeurotica.
  Prefer a 128x128 map: the model has only ever seen that size.
  The neuro_* maps were generated for this.

Closing the game shuts the server and tunnel down.

MSG

GLOB2_NEUROTICA_POLICY_SOCKET="$SOCK" "$GLOB2" "$@"
