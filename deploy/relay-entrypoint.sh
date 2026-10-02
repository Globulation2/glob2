#!/bin/sh
# Gives each relay replica its own identity and public URL, so `--scale relay=N`
# needs no per-replica configuration. Caddy forwards /relay/<id> to the
# container named <id> (see deploy/Caddyfile). Explicit GLOB2_RELAY_ID and
# GLOB2_RELAY_PUBLIC_URL values win.
set -eu
id="${GLOB2_RELAY_ID:-$(hostname)}"
case "$id" in
  *[!a-z0-9-]*|-*|'') echo "relay id '$id' must match [a-z0-9][a-z0-9-]{0,62} for Caddy routing" >&2; exit 64 ;;
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
  GLOB2_RELAY_PUBLIC_URL="${GLOB2_RELAY_PUBLIC_BASE%/}/$id"
fi
# One spool directory per relay id on the shared spool volume, so replicas
# never re-submit each other's records.
if [ -n "${GLOB2_RELAY_SPOOL_DIR:-}" ]; then
  GLOB2_RELAY_SPOOL_DIR="${GLOB2_RELAY_SPOOL_DIR%/}/$id"
  mkdir -p "$GLOB2_RELAY_SPOOL_DIR"
  export GLOB2_RELAY_SPOOL_DIR
fi
export GLOB2_RELAY_ID="$id"
[ -n "${GLOB2_RELAY_PUBLIC_URL:-}" ] && export GLOB2_RELAY_PUBLIC_URL
exec "$@"
