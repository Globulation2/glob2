#!/bin/sh
# Keep Node as the primary process so SIGTERM reaches its shutdown handlers.
set -eu
work=$(mktemp -d /tmp/glob2-skin-display.XXXXXX)
export XAUTHORITY="$work/Xauthority"
touch "$XAUTHORITY"
mkdir -p /tmp/.X11-unix
chmod 1777 /tmp/.X11-unix
cookie=$(mcookie)
xauth -f "$XAUTHORITY" add :0 MIT-MAGIC-COOKIE-1 "$cookie"
Xvfb -displayfd 3 -screen 0 128x128x24 -nolisten tcp -auth "$XAUTHORITY" 3>"$work/display" &
server=$!
cleanup() { kill "$server" 2>/dev/null || :; rm -rf "$work"; }
trap cleanup EXIT
trap 'exit 143' TERM
trap 'exit 130' INT
tries=100
while [ ! -s "$work/display" ]; do
    if ! kill -0 "$server" 2>/dev/null || [ "$tries" -eq 0 ]; then
        echo 'Skin renderer Xvfb startup failed' >&2
        exit 1
    fi
    tries=$((tries - 1))
    sleep 0.1
done
export DISPLAY=":$(cat "$work/display")"
xauth -f "$XAUTHORITY" add "$DISPLAY" MIT-MAGIC-COOKIE-1 "$cookie"
# The container init reaps Xvfb after Node has finished graceful shutdown.
trap - EXIT TERM INT
exec "$@"
