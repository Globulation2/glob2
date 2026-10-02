#!/bin/sh
# One-shot set-up run by the `init` service before the platform starts:
# 1. a first Ed25519 signing key in JWT_KEYS_DIR, unless one exists;
# 2. the shared relay key (relays' bearer token for /internal), unless it exists;
# 3. database migrations.
# Safe to run on every `docker compose up`: existing keys are never replaced.
set -eu
keys="${JWT_KEYS_DIR:-/var/lib/glob2/keys}"
relay_key="${RELAY_KEYS_FILE:-/var/lib/glob2/relay/relay.key}"
if ! ls "$keys"/*.pem >/dev/null 2>&1; then
  platform keys generate --dir "$keys"
else
  echo "signing keys present: $(cd "$keys" && ls *.pem | tr '\n' ' ')"
fi
if [ ! -s "$relay_key" ]; then
  umask 077
  node -e "process.stdout.write(require('node:crypto').randomBytes(32).toString('base64url'))" > "$relay_key.tmp"
  chmod 440 "$relay_key.tmp"
  mv "$relay_key.tmp" "$relay_key"
  echo "wrote $relay_key"
fi
exec node /app/platform/packages/db/src/cli.ts latest
