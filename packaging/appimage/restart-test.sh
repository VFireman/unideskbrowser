#!/bin/bash
# Restart test INSIDE the container: start the AppImage three times in a row with the same HOME
# (config copied from a previous live test), terminating the whole process tree in between.
# Usage: bash /work/out/restart-test.sh <AppImage> <config dir to seed>
set -uo pipefail
export APPIMAGE_EXTRACT_AND_RUN=1
APP=${1:?AppImage}; SEED=${2:?config dir}
OUT=/work/out/shots
export HOME=/tmp/restart-home; rm -rf "$HOME"; mkdir -p "$HOME/.config"
cp -a "$SEED"/. "$HOME/.config/"
export DISPLAY=:99
Xvfb :99 -screen 0 1280x800x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
XPID=$!; sleep 2
eval "$(dbus-launch --sh-syntax)"
export GTK_THEME=Adwaita:dark G_MESSAGES_DEBUG=all
ls -la "$HOME/.config/unidesk"

for n in 1 2 3; do
  "$APP" >"/tmp/run$n.log" 2>&1 &
  P=$!
  sleep 8
  WIN=$(xwininfo -root -tree 2>/dev/null | grep -c "uz.core.Unidesk")
  if kill -0 $P 2>/dev/null; then echo "start $n: running, windows=$WIN"; else echo "start $n: EXITED, windows=$WIN"; fi
  [ "$n" = 3 ] && import -window root "$OUT/shot-restart.png" 2>/dev/null
  # terminate the runtime and the extracted binary it spawned
  kill $P 2>/dev/null
  for pid in $(pgrep -f appimage_extracted); do kill $pid 2>/dev/null; done
  sleep 2
  for pid in $(pgrep -f appimage_extracted); do kill -9 $pid 2>/dev/null; done
  sleep 1
  echo "  leftover processes: $(pgrep -fc appimage_extracted)"
  echo "  log tail:"; grep -v -E "GLib-GIO-DEBUG|Gdk-DEBUG|Gtk-DEBUG|palette" "/tmp/run$n.log" | tail -4 | cut -c1-160
done
kill $XPID 2>/dev/null; kill "${DBUS_SESSION_BUS_PID:-0}" 2>/dev/null
