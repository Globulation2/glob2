#!/bin/bash
# usage: run.sh BENCH OUTDIR
B=$1; O=$2; P=/Users/bradley/glob2-claude/.claude/worktrees/parallel-frame-prototype/artifacts/threading
mkdir -p $O
cap() { # name env...
 n=$1; shift
 env "$@" PROFILE_FRAMES=3 PROFILE_WARMUP=2 PROFILE_NO_PRESENT=1 PROFILE_CAPTURE=$O/$n.bmp GLOB2_USER_DATA_DIR=/tmp/parity2/profile \
   $B -G -s 1280x800 -m -F > $O/$n.log 2>&1 || echo "FAIL $n"
}
for save in fix-balanced/checkpoint-30000 fix-oazis/checkpoint-30000 fix-oazis/checkpoint-15000; do
 s=$(echo $save | tr '/' '_')
 for zoom in 1 0.5 1.37; do for mode in gui map; do
  cap $s-z$zoom-$mode PROFILE_SAVE=$P/$save.game.gz PROFILE_ZOOM=$zoom PROFILE_MODE=$mode
 done; done
 for sel in building flag unit; do cap $s-select-$sel PROFILE_SAVE=$P/$save.game.gz PROFILE_MODE=gui PROFILE_SELECT=$sel; done
 cap $s-tool-inn PROFILE_SAVE=$P/$save.game.gz PROFILE_MODE=gui PROFILE_TOOL=inn
done
