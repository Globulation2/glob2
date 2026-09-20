#!/bin/bash
# Play Glob2 with a live, network-driven Neurotica.
#
# Neurotica is already selectable in the GUI (see AINames.h), but with no policy
# server it falls back to an inert identity field and does nothing at all. This
# supplies the missing half: it starts a policy server on therig's GPU, forwards
# its Unix socket here over SSH, and launches the game pointed at that socket.
#
# Inference runs on therig. The model chooses a delay of 1–25 ticks between
# orders, so network latency can affect interactive speed.
#
# Uses the same NPS6 semantic-order decoder as BC, PPO and evaluation.
# Set CKPT to a new order checkpoint on the rig; old field checkpoints fail.
set -eu
CKPT=${CKPT:?set CKPT to a new semantic-order checkpoint on the rig}
printf -v CKPT_ARG '%q' "$CKPT"
RIG=${RIG:-therig.local}
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
    --checkpoint $CKPT_ARG --socket $SOCK --device cuda \
    --seed 0 > /tmp/gui_serve.log 2>&1 < /dev/null &
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
