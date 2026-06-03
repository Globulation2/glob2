#!/usr/bin/env bash
#
# ai-benchmark.sh — head-to-head AI win-rate harness for Globulation 2.
#
# Runs N headless games of a fixed map + matchup and reports win rate with a
# 95% confidence interval. This is the fitness function for hand-crafted AI
# work: "better than Nicowar" is only meaningful as a measured win rate over
# many games.
#
# It drives the C++ binary's existing headless flags (no Rust involved):
#   glob2 -test-games-nox 1 --map <name> --matchup <a,b,...>
# pins the per-game RNG via GLOB2_TEST_SEED, and scrapes the
# "GLOB2_GAME_END ... winner_team=N ..." line from stdout. See
# glob2/docs/headless-replays.md for the flag/format reference.
#
# Seeds are deterministic (seed = --seed-base + game index), so a benchmark
# is reproducible: re-running compares a *new* AI against the *same* set of
# game scenarios an old one faced, and any single game can be replayed for
# debugging. Without a pinned seed, back-to-back sub-second games collide on
# the wall-clock seed and you get duplicate, non-independent samples.
#
# --swap-sides (2-AI matchups only) runs each seed under BOTH team orderings
# and aggregates by AI identity, cancelling map/start-position bias. A
# Nicowar-vs-Nicowar mirror on SmallForTwo comes out ~35/65 by side, so the
# side a bot spawns on confounds any single-ordering result. Use it for any
# "is X better than Y" measurement.
#
# Build the binary with `scons release=1 -j16` first — the default debug
# build runs ~10x slower.
#
# Usage:
#   tools/ai-benchmark.sh --map SmallForTwo --matchup nicowar,nicowar --games 40
#   tools/ai-benchmark.sh --map SmallForTwo --matchup cortex,nicowar --games 100 --swap-sides
#
# Options:
#   --map <name>       Map filename without .map (resolved as maps/<name>.map). Required.
#   --matchup <list>   Comma-separated AI per team; matchup[k] plays team k. Required.
#   --games <N>        Games to run (default: 40). With --swap-sides this is
#                      seed count; total games played is 2*N.
#   --swap-sides       Run each seed under both orderings; report by AI identity.
#                      Requires a 2-entry matchup.
#   --seed-base <N>    First GLOB2_TEST_SEED (default: 1). Seeds run base..base+games-1.
#   --bin <path>       glob2 binary (default: build/src/glob2 under the repo).
#   --out <dir>        Per-game logs/replays dir (default: temp dir, cleaned on exit).
#   --keep             Keep per-game logs/replays.
#
# A 90k-tick timeout (winner_team=-1) is a draw: counted, but a win for no one.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"   # tools/ -> glob2/

MAP=""
MATCHUP=""
GAMES=40
SWAP=0
SEED_BASE=1
BIN="$REPO_DIR/build/src/glob2"
OUT=""
KEEP=0

while [ $# -gt 0 ]; do
	case "$1" in
		--map)        MAP="$2"; shift 2 ;;
		--matchup)    MATCHUP="$2"; shift 2 ;;
		--games)      GAMES="$2"; shift 2 ;;
		--swap-sides) SWAP=1; shift ;;
		--seed-base)  SEED_BASE="$2"; shift 2 ;;
		--bin)        BIN="$2"; shift 2 ;;
		--out)        OUT="$2"; shift 2 ;;
		--keep)       KEEP=1; shift ;;
		-h|--help)    sed -n '2,55p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*)            echo "error: unknown argument '$1' (try --help)" >&2; exit 2 ;;
	esac
done

# --- validation -------------------------------------------------------------
if [ -z "$MAP" ] || [ -z "$MATCHUP" ]; then
	echo "error: --map and --matchup are required (try --help)" >&2; exit 2
fi
if [ ! -x "$BIN" ]; then
	echo "error: glob2 binary not found or not executable: $BIN" >&2
	echo "       build it with: (cd '$REPO_DIR' && scons release=1 -j16)" >&2
	exit 2
fi
case "$GAMES" in ''|*[!0-9]*) echo "error: --games must be a positive integer" >&2; exit 2 ;; esac
case "$SEED_BASE" in ''|*[!0-9]*) echo "error: --seed-base must be a non-negative integer" >&2; exit 2 ;; esac
[ "$GAMES" -lt 1 ] && { echo "error: --games must be >= 1" >&2; exit 2; }

NTEAMS=$(printf '%s' "$MATCHUP" | awk -F, '{print NF}')
if [ "$SWAP" -eq 1 ] && [ "$NTEAMS" -ne 2 ]; then
	echo "error: --swap-sides requires a 2-entry --matchup (got $NTEAMS)" >&2; exit 2
fi

# --- output dir -------------------------------------------------------------
CLEANUP=0
if [ -z "$OUT" ]; then
	OUT="$(mktemp -d "${TMPDIR:-/tmp}/glob2-bench.XXXXXX")"
	[ "$KEEP" -eq 0 ] && CLEANUP=1
fi
mkdir -p "$OUT"
cleanup() { [ "$CLEANUP" -eq 1 ] && rm -rf "$OUT"; }
trap cleanup EXIT

# --- run one game: run_game <seed> <matchup> <tag>; sets $WINNER and $TICKS --
WINNER=""; TICKS=""
run_game() {
	local seed="$1" matchup="$2" tag="$3"
	local log="$OUT/game-$tag.log"
	GLOB2_TEST_SEED="$seed" GLOB2_REPLAY_PATH="$OUT/game-$tag.replay" \
		"$BIN" -test-games-nox 1 --map "$MAP" --matchup "$matchup" >"$log" 2>&1
	local line
	line="$(grep -m1 '^GLOB2_GAME_END' "$log" 2>/dev/null)"
	if [ -z "$line" ]; then WINNER="ERR"; TICKS=""; return 1; fi
	WINNER="$(printf '%s\n' "$line" | sed -n 's/.*winner_team=\(-\{0,1\}[0-9]\{1,\}\).*/\1/p')"
	TICKS="$(printf '%s\n' "$line" | sed -n 's/.*ticks=\([0-9]\{1,\}\).*/\1/p')"
	return 0
}

# print a win-rate line with a 95% Wald CI: report_rate <label> <wins> <denom>
report_rate() {
	awk -v lbl="$1" -v w="$2" -v n="$3" 'BEGIN {
		if (n == 0) { printf "  %-28s   (no completed games)\n", lbl; exit }
		p = w / n; se = sqrt(p * (1 - p) / n);
		lo = p - 1.96*se; if (lo < 0) lo = 0;
		hi = p + 1.96*se; if (hi > 1) hi = 1;
		printf "  %-28s %3d/%-3d  %5.1f%%  (95%% CI %4.1f%%-%4.1f%%)\n",
			lbl, w, n, 100*p, 100*lo, 100*hi;
	}'
}

echo "glob2 AI benchmark"
echo "  binary : $BIN"
echo "  map    : $MAP"
echo "  matchup: $MATCHUP  (${NTEAMS} teams)"
echo "  seeds  : ${SEED_BASE}..$((SEED_BASE + GAMES - 1))$([ "$SWAP" -eq 1 ] && echo ', both orderings')"
echo "  logs   : $OUT"
echo

errors=0

if [ "$SWAP" -eq 1 ]; then
	# ---- side-swapped 2-AI mode: aggregate by AI identity --------------------
	OLDIFS="$IFS"; IFS=','; set -- $MATCHUP; IFS="$OLDIFS"
	AI_A="$1"; AI_B="$2"
	rev="$AI_B,$AI_A"
	a_wins=0; b_wins=0; draws=0; completed=0
	i=0
	while [ "$i" -lt "$GAMES" ]; do
		seed=$((SEED_BASE + i))
		# ordering 1: A=team0, B=team1
		if run_game "$seed" "$MATCHUP" "$(printf '%04d' "$i")-fwd"; then
			completed=$((completed+1))
			case "$WINNER" in
				0) a_wins=$((a_wins+1)) ;;
				1) b_wins=$((b_wins+1)) ;;
				-1) draws=$((draws+1)) ;;
			esac
		else errors=$((errors+1)); fi
		# ordering 2: B=team0, A=team1
		if run_game "$seed" "$rev" "$(printf '%04d' "$i")-rev"; then
			completed=$((completed+1))
			case "$WINNER" in
				0) b_wins=$((b_wins+1)) ;;
				1) a_wins=$((a_wins+1)) ;;
				-1) draws=$((draws+1)) ;;
			esac
		else errors=$((errors+1)); fi
		printf '  seed %6d: %s=%d %s=%d draws=%d\n' "$seed" "$AI_A" "$a_wins" "$AI_B" "$b_wins" "$draws"
		i=$((i+1))
	done
	echo
	echo "results over $((GAMES*2)) games ($completed completed, $errors errored, side bias cancelled):"
	report_rate "$AI_A (either side)" "$a_wins" "$completed"
	report_rate "$AI_B (either side)" "$b_wins" "$completed"
	report_rate "draws (timeouts)"    "$draws"  "$completed"
else
	# ---- per-team mode: tally by team index ---------------------------------
	t=0; while [ "$t" -lt "$NTEAMS" ]; do wins[$t]=0; t=$((t+1)); done
	draws=0; completed=0
	i=0
	while [ "$i" -lt "$GAMES" ]; do
		seed=$((SEED_BASE + i))
		if run_game "$seed" "$MATCHUP" "$(printf '%04d' "$i")"; then
			completed=$((completed+1))
			if [ "$WINNER" = "-1" ]; then
				draws=$((draws+1))
				printf '  seed %6d: draw   (timeout, %s ticks)\n' "$seed" "${TICKS:-?}"
			else
				wins[$WINNER]=$(( ${wins[$WINNER]} + 1 ))
				printf '  seed %6d: team %s wins (%s ticks)\n' "$seed" "$WINNER" "${TICKS:-?}"
			fi
		else
			errors=$((errors+1))
			printf '  seed %6d: ERROR (no GLOB2_GAME_END; see %s)\n' "$seed" "$OUT/game-$(printf '%04d' "$i").log"
		fi
		i=$((i+1))
	done
	echo
	echo "results over $GAMES games ($completed completed, $errors errored):"
	if [ "$completed" -eq 0 ]; then echo "  no games completed" >&2; exit 1; fi
	OLDIFS="$IFS"; IFS=','; set -- $MATCHUP; IFS="$OLDIFS"
	t=0
	for ai in "$@"; do
		report_rate "team $t ($ai)" "${wins[$t]}" "$completed"
		t=$((t+1))
	done
	report_rate "draws (timeouts)" "$draws" "$completed"
fi
