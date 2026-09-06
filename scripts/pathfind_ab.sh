#!/usr/bin/env bash
# A/B harness for pathfinding work: Nicowar vs Nicowar, 10 game minutes,
# one team on the baseline pathfinder and the other on the alternative
# (GLOB2_PATHFIND_ALT_TEAMS bitmask). Each seed is run twice with the
# assignment mirrored so map/start asymmetry cancels out.
#
# usage: scripts/pathfind_ab.sh [-m map] [-s seeds] [-t ticks] [-o outdir] [-b binary]
set -euo pipefail

MAP=Mazury
SEEDS="1 2 3 4 5"
TICKS=15000
OUT=.scratch/pathfind-ab
BIN=./build/src/glob2
while getopts "m:s:t:o:b:" opt; do
	case $opt in
		m) MAP=$OPTARG ;;
		s) SEEDS=$OPTARG ;;
		t) TICKS=$OPTARG ;;
		o) OUT=$OPTARG ;;
		b) BIN=$OPTARG ;;
		*) exit 1 ;;
	esac
done
mkdir -p "$OUT"

run_one() {
	local seed=$1 altmask=$2 tag=$3
	local log="$OUT/$MAP-s$seed-$tag.log"
	local start end
	start=$(date +%s.%N)
	GLOB2_PATHFIND_ALT_TEAMS=$altmask GLOB2_PATHFIND_STATS=1 GLOB2_MAX_TICKS=$TICKS \
	GLOB2_TEST_SEED=$seed GLOB2_TEAM_TIMELINE=1 GLOB2_REPLAY_PATH="$OUT/$MAP-s$seed-$tag.replay" \
		"$BIN" -test-games-nox 1 --map "$MAP" --matchup nicowar,nicowar > "$log" 2>&1 || true
	end=$(date +%s.%N)
	echo "WALL=$(echo "$end - $start" | bc)" >> "$log"
}

# team bit 0 = team 0, bit 1 = team 1
for seed in $SEEDS; do
	run_one "$seed" 2 altT1 &   # team 1 on alternative
	run_one "$seed" 1 altT0 &   # team 0 on alternative
	wait
done

# Summarise: for each run, per team: units, buildings, deliveries, random-while-working, swim share.
summary() {
	local log=$1 team=$2
	local units bld del rnd walk swim card diag
	units=$(grep "GLOB2_TL team=$team " "$log" | tail -1 | sed -E 's/.* units=([0-9]+).*/\1/')
	bld=$(grep "GLOB2_TL team=$team " "$log" | tail -1 | sed -E 's/.* bld=([0-9]+).*/\1/')
	eval "$(grep "GLOB2_PF_TEAM team=$team " "$log" | sed -E 's/GLOB2_PF_TEAM team=[0-9]+ //; s/([a-z_]+)=([0-9]+)/\1=\2;/g')"
	printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$units" "$bld" "$deliveries" "$random_while_working" "$moves_walk" "$moves_swim" "$moves_diagonal"
}
echo "map=$MAP ticks=$TICKS"
printf 'seed\tside\talgo\tunits\tbld\tdeliv\trandom_working\twalk\tswim\tdiag\twall_s\n'
for seed in $SEEDS; do
	for tag in altT1 altT0; do
		log="$OUT/$MAP-s$seed-$tag.log"
		wall=$(grep WALL= "$log" | cut -d= -f2)
		for team in 0 1; do
			if [ "$tag" = altT1 ]; then algo=$([ $team = 1 ] && echo ALT || echo BASE); else algo=$([ $team = 0 ] && echo ALT || echo BASE); fi
			printf '%s\tteam%s\t%s\t%s\t%s\n' "$seed" "$team" "$algo" "$(summary "$log" $team)" "$wall"
		done
	done
done | column -t
