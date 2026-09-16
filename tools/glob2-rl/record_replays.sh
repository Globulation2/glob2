#!/bin/bash
# Record watchable replays of Neurotica playing, for human inspection.
#
# A .replay embeds the whole tick-0 game state (map included) plus the order
# stream, so the file is self-contained: it plays back on any machine with a
# matching VERSION_MINOR, with no policy server and no GPU. During playback the
# engine still constructs the AI players but discards their orders (see
# Engine::executeOrdersAndStep), and Neurotica's policy socket fails inert when
# GLOB2_NEUROTICA_POLICY_SOCKET is unset, so nothing tries to reach the net.
#
# Each replay is named with its outcome so you know what you are opening:
#   neurotica_vs_castor_92431_WIN.replay
#
# Usage: record_replays.sh <checkpoint> <opponent> [games] [max-ticks]
set -u
CKPT=${1:?usage: record_replays.sh <checkpoint> <opponent> [games] [max-ticks]}
OPP=${2:?need an opponent: numbi|castor|warrush|nicowar|cortex|cabino|maxima}
GAMES=${3:-3}
MAXTICKS=${4:-40000}

ROOT=$HOME/neurotica
G=$ROOT/glob2
OUT=$ROOT/replays
SOCK=/tmp/neurotica_replay.sock
mkdir -p "$OUT"

# Own socket and own server process so this never disturbs a running
# self-play loop (which uses /tmp/neurotica_sp.sock).
pkill -f "[n]eurotica_serve.py.*neurotica_replay" 2>/dev/null || true
sleep 1
CUDA_VISIBLE_DEVICES=1 setsid nohup $ROOT/.venv/bin/python $ROOT/rl/neurotica_serve.py \
  --checkpoint "$CKPT" --socket $SOCK --device cuda --max-batch 8 --top-k --placements 12 \
  > /tmp/replay_serve.log 2>&1 < /dev/null &
sleep 12
if [ ! -S $SOCK ]; then echo "policy server failed to start; see /tmp/replay_serve.log" >&2; exit 1; fi

mapfile -t GENS < $ROOT/generators.txt
for i in $(seq 1 "$GAMES"); do
  ID=$((90000 + RANDOM))
  GEN=${GENS[$((RANDOM % ${#GENS[@]}))]}
  MAP="rp_${ID}"
  timeout 180 $G/build/src/glob2 --generate-map "$GEN" --output "$G/maps/$MAP.map" \
    --width 128 --height 128 --teams 2 --seed "$ID" >/dev/null 2>&1 || continue

  TMP="$OUT/.tmp_${ID}.replay"
  LINE=$(GLOB2_TEST_SEED=$ID \
    GLOB2_NEUROTICA_POLICY_SOCKET=$SOCK \
    GLOB2_TEST_MAX_TICKS=$MAXTICKS \
    GLOB2_REPLAY_PATH="$TMP" \
    timeout 1200 $G/build/src/glob2 -test-games-nox 1 --map "$MAP" \
      --matchup neurotica,"$OPP" 2>&1 | grep -o "GLOB2_GAME_END.*")
  rm -f "$G/maps/$MAP.map"

  # Neurotica is team 0 in the matchup, so winner_team=0 is a win for us.
  case "$LINE" in
    *"winner_team=0"*) RES=WIN;;
    *"winner_team=1"*) RES=LOSS;;
    *)                 RES=CAP;;
  esac
  TICKS=$(echo "$LINE" | grep -o "ticks=[0-9]*" | cut -d= -f2)
  if [ -f "$TMP" ]; then
    mv "$TMP" "$OUT/neurotica_vs_${OPP}_${ID}_${RES}.replay"
    echo "game $i/$GAMES: $RES after ${TICKS:-?} ticks (gen $GEN)  -> neurotica_vs_${OPP}_${ID}_${RES}.replay"
  else
    echo "game $i/$GAMES: $RES after ${TICKS:-?} ticks (gen $GEN)  -> NO REPLAY WRITTEN"
  fi
done

pkill -f "[n]eurotica_serve.py.*neurotica_replay" 2>/dev/null || true
echo RECORD_DONE
