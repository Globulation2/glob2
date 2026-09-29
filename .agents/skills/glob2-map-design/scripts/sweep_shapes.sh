#!/bin/bash
# Generate one generator across map shapes and colony counts, three seeds each, and print the cells
# that did not all succeed with the refusal messages. Run from the repository root with bash (zsh
# does not split "$wh" into width and height). A successful --generate-map prints no
# "Map command" failure line; a refusal prints "[validation]: <message>".
#   bash .agents/skills/glob2-map-design/scripts/sweep_shapes.sh glacis [--set key=value ...]
generator=$1; shift
extra=("$@")
out=$(mktemp -d)
for wh in "64 64" "128 128" "256 256" "512 512" "512 256" "256 512" "128 512" "256 128"; do
	set -- $wh
	for teams in 1 2 3 4 6 8 12; do
		ok=0; failures=""
		for seed in 1 2 3; do
			line=$(build/src/glob2 --generate-map "$generator" --seed $seed --width $1 --height $2 \
				--teams $teams "${extra[@]}" --output "$out/x.map" 2>&1 | grep "Map command")
			if echo "$line" | grep -q "\]:"; then
				failures="$failures | $(echo "$line" | sed 's/.*\]: //')"
			else
				ok=$((ok + 1))
			fi
		done
		[ $ok -lt 3 ] && echo "$1x$2 $teams colonies: $ok/3$failures"
	done
done
rm -rf "$out"
