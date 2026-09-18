#!/bin/bash
# Continuous Neurotica self-play: rollouts and PPO running side by side.
#
# The server samples latents and records trajectories; the driver plays games
# on freshly generated maps against the league; the learner consumes finished
# episodes, updates, and writes policy.pt, which the server hot-reloads. The
# learner deletes episodes it has used, which is also what keeps the rollout
# directory from filling the disk.
#
# Usage: selfplay_loop.sh <bc-checkpoint> [parallel-games]
set -eu
INIT=${1:?usage: selfplay_loop.sh <bc-checkpoint> [parallel]}
PAR=${2:-4}
ROOT=$HOME/neurotica
VENV=$ROOT/.venv/bin/python
SOCK=/tmp/neurotica_sp.sock
ROLL=$ROOT/rollouts
mkdir -p "$ROLL" "$ROOT/ppo"

# Seed the PPO policy from behaviour cloning so the server has something to
# hot-reload from on the very first check.
cp "$INIT" "$ROOT/ppo/policy.pt"

# Only this loop's own server. A bare pkill on neurotica_serve.py also takes
# down any eval arm's server running alongside, and a Neurotica whose server
# vanishes fails inert -- the eval then quietly measures a do-nothing AI.
for p in $(pgrep -f "neurotica_serve.py.*neurotica_sp.sock"); do kill "$p" 2>/dev/null || true; done
sleep 2

CUDA_VISIBLE_DEVICES=1 setsid nohup $VENV $ROOT/rl/neurotica_serve.py \
  --checkpoint "$ROOT/ppo/policy.pt" --socket $SOCK --device cuda \
  --max-batch 16 --sample --use-count --ratio --placements 8 --record-dir "$ROLL" \
  --reload-from "$ROOT/ppo/policy.pt" \
  > $ROOT/serve_sp.log 2>&1 < /dev/null &
sleep 12

CUDA_VISIBLE_DEVICES=0 setsid nohup $VENV $ROOT/rl/neurotica_ppo.py \
  --init "$INIT" --rollouts "$ROLL" --out "$ROOT/ppo" \
  --iterations 1000000 --minibatch 12 \
  > $ROOT/ppo.log 2>&1 < /dev/null &
sleep 2

setsid nohup $VENV $ROOT/rl/neurotica_selfplay.py \
  --socket $SOCK --record-dir "$ROLL" --parallel "$PAR" \
  --games 1000000 --policy-period 25 --start-id 20000 \
  --max-ticks 40000 --max-pending 14 \
  > $ROOT/rollout.log 2>&1 < /dev/null &

echo "self-play running: serve_sp.log, ppo.log, rollout.log"
