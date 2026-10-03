#!/bin/sh
# Gives each relay replica a stable identity and its public URL, so `--scale relay=N`
# needs no per-replica configuration (explicit GLOB2_RELAY_ID and
# GLOB2_RELAY_PUBLIC_URL values win).
#
# Identity: the first free slot relay-1, relay-2, ... on the shared spool volume,
# claimed with flock(1) on <spool>/.slots/relay-<n>.lock. The lock's descriptor is
# inherited by the relay (exec below), so the slot stays claimed exactly as long
# as the relay runs and is free again the moment it exits, crashes or its
# container is recreated. A recreated relay therefore gets an id from the same
# small set, finds its spool directory <spool>/<id> again and re-submits the match
# records and end reports spooled there; pinned relay keys (relayId:key) keep
# working. Without a spool directory (or flock) the id is the container host name.
#
# Routing: Caddy forwards /relay/<label> to the container whose host name is
# <label> (deploy/Caddyfile), so the public URL ends in this container's host
# name, which Docker resolves on the backend network. The relay id and the URL
# label are independent: the platform stores the URL from each registration.
#
# Spools left behind: before starting, the relay adopts the spooled matches of
# every other spool directory that no running relay holds (a slot above the
# current replica count after a scale-down, or a directory named by a container
# id from before stable ids), so they are re-submitted instead of orphaned.
set -eu

route=$(hostname)
spool_root=${GLOB2_RELAY_SPOOL_DIR:-}
spool_root=${spool_root%/}
max_slots=${GLOB2_RELAY_MAX_SLOTS:-64}

id=${GLOB2_RELAY_ID:-}
if [ -z "$id" ] && [ -n "$spool_root" ] && command -v flock >/dev/null 2>&1; then
  mkdir -p "$spool_root/.slots"
  n=1
  while [ "$n" -le "$max_slots" ]; do
    # Descriptor 9 stays open (and locked) in the relay after exec.
    exec 9>"$spool_root/.slots/relay-$n.lock"
    if flock -n 9; then
      id="relay-$n"
      break
    fi
    exec 9>&-
    n=$((n + 1))
  done
fi
id=${id:-$route}
case "$id" in
  *[!A-Za-z0-9._-]*|'') echo "relay id '$id' must match [A-Za-z0-9._-]{1,64}" >&2; exit 64 ;;
esac
case "$route" in
  *[!a-z0-9-]*|-*|'') echo "host name '$route' must match [a-z0-9][a-z0-9-]{0,62} for Caddy routing" >&2; exit 64 ;;
esac

# wss://<host>/relay from GLOB2_PUBLIC_ORIGIN (https://<host>) unless given.
if [ -z "${GLOB2_RELAY_PUBLIC_BASE:-}" ] && [ -n "${GLOB2_PUBLIC_ORIGIN:-}" ]; then
  case "$GLOB2_PUBLIC_ORIGIN" in
    https://*) GLOB2_RELAY_PUBLIC_BASE="wss://${GLOB2_PUBLIC_ORIGIN#https://}" ;;
    http://*) GLOB2_RELAY_PUBLIC_BASE="ws://${GLOB2_PUBLIC_ORIGIN#http://}" ;;
    *) echo "GLOB2_PUBLIC_ORIGIN must start with https:// or http://" >&2; exit 64 ;;
  esac
  GLOB2_RELAY_PUBLIC_BASE="${GLOB2_RELAY_PUBLIC_BASE%/}/relay"
fi
if [ -z "${GLOB2_RELAY_PUBLIC_URL:-}" ] && [ -n "${GLOB2_RELAY_PUBLIC_BASE:-}" ]; then
  GLOB2_RELAY_PUBLIC_URL="${GLOB2_RELAY_PUBLIC_BASE%/}/$route"
fi

# One spool directory per relay id on the shared spool volume, so replicas
# never re-submit each other's records.
if [ -n "$spool_root" ]; then
  own="$spool_root/$id"
  mkdir -p "$own"
  if command -v flock >/dev/null 2>&1; then
    for dir in "$spool_root"/*; do
      [ -d "$dir" ] || continue
      name=${dir##*/}
      [ "$name" = "$id" ] && continue
      case "$name" in
        relay-[0-9]*)
          # Held by a running relay: not ours to take.
          exec 8>"$spool_root/.slots/$name.lock"
          if ! flock -n 8; then
            exec 8>&-
            continue
          fi
          ;;
        *)
          # A pre-stable-id directory (container id); its relay is gone once
          # every relay runs this script, as compose recreates them together.
          exec 8>"$spool_root/.slots/adopt.lock"
          flock 8
          ;;
      esac
      adopted=0
      # The record first, then the end report: a spool entry counts once both exist.
      for end in "$dir"/*.end.json; do
        [ -f "$end" ] || continue
        match=${end##*/}
        match=${match%.end.json}
        [ -e "$own/$match.end.json" ] && continue
        if [ -f "$dir/$match.g2mr" ]; then
          mv "$dir/$match.g2mr" "$own/$match.g2mr"
          mv "$end" "$own/$match.end.json"
          adopted=$((adopted + 1))
        fi
      done
      [ "$adopted" -gt 0 ] && echo "relay $id: adopted $adopted spooled match(es) from $name" >&2
      rmdir "$dir" 2>/dev/null || true
      exec 8>&-
    done
  fi
  GLOB2_RELAY_SPOOL_DIR="$own"
  export GLOB2_RELAY_SPOOL_DIR
fi
export GLOB2_RELAY_ID="$id"
[ -n "${GLOB2_RELAY_PUBLIC_URL:-}" ] && export GLOB2_RELAY_PUBLIC_URL
exec "$@"
