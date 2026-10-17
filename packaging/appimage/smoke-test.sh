#!/bin/bash
# Smoke test of the AppImage under Xvfb (runs INSIDE the unidesk-build container).
# Usage: bash /work/scripts/smoke-test.sh /work/out/Unidesk-x86_64.AppImage [seconds] [extra args...]
set -uo pipefail
export APPIMAGE_EXTRACT_AND_RUN=1
APP=${1:?AppImage path}
WAIT=${2:-8}
shift 2 2>/dev/null || true
OUT=/work/out/shots
TAG=${TAG:-01}
mkdir -p "$OUT"
rm -f "$OUT"/shot-$TAG.png "$OUT"/app-$TAG.log

export HOME=/tmp/unidesk-home
mkdir -p "$HOME/.config"
# optional seed: a prepared config dir with profiles (copied into the fresh HOME)
if [ -n "${SEED_DIR:-}" ] && [ -d "$SEED_DIR" ]; then cp -a "$SEED_DIR"/. "$HOME/.config/"; fi
export DISPLAY=:99
Xvfb :99 -screen 0 1600x1000x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
XPID=$!
sleep 2
eval "$(dbus-launch --sh-syntax)"
export GTK_THEME=Adwaita:dark
export G_MESSAGES_DEBUG=all

echo "=== starting $APP $*"
"$APP" "$@" >"$OUT/app-$TAG.log" 2>&1 &
APID=$!
for i in $(seq 1 "$WAIT"); do
  sleep 1
  if ! kill -0 $APID 2>/dev/null; then echo "app exited early (after ${i}s)"; break; fi
done
import -window root "$OUT/shot-$TAG.png" 2>/dev/null || xwd -root -silent | convert xwd:- "$OUT/shot-$TAG.png"
echo "=== windows:"; xwininfo -root -tree 2>/dev/null | grep -i -E 'unidesk|remmina' | head -20
if kill -0 $APID 2>/dev/null; then echo "app still running: OK"; kill $APID; sleep 1; kill -9 $APID 2>/dev/null; else echo "APP NOT RUNNING"; fi
# keep the configuration the application wrote (the container's /tmp is discarded)
rm -rf "$OUT/config-$TAG"; cp -a "$HOME/.config" "$OUT/config-$TAG" 2>/dev/null || true
kill $XPID 2>/dev/null
kill "${DBUS_SESSION_BUS_PID:-0}" 2>/dev/null
echo "=== app log (grep warnings/errors):"; grep -E -i "critical|warning|error|unidesk|vault|imported" "$OUT/app-$TAG.log" | grep -v "GLib-GIO-DEBUG\|Gdk-DEBUG" | head -30
ls -la "$OUT" | grep "$TAG"
