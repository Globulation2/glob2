#!/bin/sh
# One-shot set-up run by the `init` service before the platform starts:
# 1. a first Ed25519 signing key in JWT_KEYS_DIR, unless one exists;
# 2. the shared relay key (relays' bearer token for /internal), unless it exists;
# 3. the engine agents' key (their bearer token for /internal/v1/engine), unless it exists;
# 4. a password per database role (migrator, api, worker), unless it exists;
# 5. the database roles, as the superuser (ADMIN_DATABASE_URL): created or
#    updated, and an older database's objects handed to glob2_migrator;
# 6. database migrations, as glob2_migrator.
# Safe to run on every `docker compose up`: existing keys and passwords are
# never replaced (delete a password file to rotate that role's password).
set -eu
keys="${JWT_KEYS_DIR:-/var/lib/glob2/keys}"
relay_key="${RELAY_KEYS_FILE:-/var/lib/glob2/relay/relay.key}"
agent_key="${ENGINE_AGENT_KEYS_FILE:-/var/lib/glob2/engine-agent/agent.key}"

# Writes 32 random bytes (base64url) to $1 unless it already holds a secret.
secret() {
  if [ ! -s "$1" ]; then
    umask 077
    node -e "process.stdout.write(require('node:crypto').randomBytes(32).toString('base64url'))" > "$1.tmp"
    chmod 400 "$1.tmp"
    mv "$1.tmp" "$1"
    echo "wrote $1"
  fi
}

if ! ls "$keys"/*.pem >/dev/null 2>&1; then
  platform keys generate --dir "$keys"
else
  echo "signing keys present: $(cd "$keys" && ls *.pem | tr '\n' ' ')"
fi
secret "$relay_key"
# Relays read the relay key as another user in some set-ups (group-readable).
chmod 440 "$relay_key"
secret "$agent_key"
for file in "${DB_MIGRATOR_PASSWORD_FILE:-}" "${DB_API_PASSWORD_FILE:-}" "${DB_WORKER_PASSWORD_FILE:-}"; do
  if [ -n "$file" ]; then secret "$file"; fi
done

if [ -n "${ADMIN_DATABASE_URL:-}" ]; then
  node /app/platform/packages/db/src/cli.ts roles
fi
exec node /app/platform/packages/db/src/cli.ts latest
