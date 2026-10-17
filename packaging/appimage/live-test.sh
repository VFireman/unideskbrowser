#!/bin/bash
# Live test: the Unidesk AppImage connects to a real RDP or SSH server under Xvfb (runs INSIDE the unidesk-build container).
# Usage: bash /work/out/live-test.sh <AppImage> rdp|ssh <host> <user> <password> [wait-seconds]
# Env: TAG (shot/log suffix), KNOWN_HOSTS (one known_hosts line for the SSH host)
set -uo pipefail
export APPIMAGE_EXTRACT_AND_RUN=1
APP=${1:?AppImage path}; KIND=${2:?rdp|ssh}; HOST=${3:?host}; USER_=${4:?user}; PASS=${5:?password}; WAIT=${6:-30}
OUT=/work/out/shots
TAG=${TAG:-live-$KIND}
mkdir -p "$OUT"
rm -f "$OUT/shot-$TAG"*.png "$OUT/app-$TAG.log"

export HOME=/tmp/unidesk-home
rm -rf "$HOME"; mkdir -p "$HOME/.config" "$HOME/.ssh"; chmod 700 "$HOME/.ssh"
export DISPLAY=:99
Xvfb :99 -screen 0 1600x1000x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
XPID=$!
sleep 2
eval "$(dbus-launch --sh-syntax)"
export GTK_THEME=Adwaita:dark
export G_MESSAGES_DEBUG=all

if [ "$KIND" = ssh ] && [ -n "${KNOWN_HOSTS:-}" ]; then
  printf '%s\n' "$KNOWN_HOSTS" > "$HOME/.ssh/known_hosts"; chmod 600 "$HOME/.ssh/known_hosts"
  echo "=== seeded ~/.ssh/known_hosts"
fi
if [ "$KIND" = rdp ]; then
  echo "=== warm-up: xfreerdp with /cert:tofu stores the server certificate in ~/.config/freerdp/server for the same HOME"
  timeout 12 xfreerdp /v:"$HOST" /u:"$USER_" /p:"$PASS" /cert:tofu /size:800x600 /log-level:WARN >/tmp/xfreerdp-warm.log 2>&1
  echo "xfreerdp exit=$? (124 = killed by timeout while connected)"
  grep -E "Logon Info|ERROR" /tmp/xfreerdp-warm.log | grep -v -E "Kerberos|krb5" | tail -3
  ls -la "$HOME/.config/freerdp/server/" 2>/dev/null || echo "(no certificate store)"
fi

# The first run creates unidesk.pref with a fresh secret; the password is encrypted with it for the -c URI.
ENC=$(printf '%s\n' "$PASS" | "$APP" --encrypt-password 2>/dev/null | sed -n 's/^Encrypted password: //p')
echo "=== encrypted password: ${#ENC} chars"
URI="$KIND://$USER_:$ENC@$HOST"

echo "=== starting $APP -c $KIND://$USER_:***@$HOST (wait $WAIT s)"
"$APP" -c "$URI" >"$OUT/app-$TAG.log" 2>&1 &
APID=$!
for i in $(seq 1 "$WAIT"); do
  sleep 1
  if ! kill -0 $APID 2>/dev/null; then echo "app exited early (after ${i}s)"; break; fi
  if [ "$i" -eq 8 ] || [ "$i" -eq 18 ]; then import -window root "$OUT/shot-$TAG-${i}s.png" 2>/dev/null; fi
done
import -window root "$OUT/shot-$TAG.png" 2>/dev/null || xwd -root -silent | convert xwd:- "$OUT/shot-$TAG.png"
echo "=== windows:"; xwininfo -root -tree 2>/dev/null | grep -i -E 'unidesk|remmina' | head
if kill -0 $APID 2>/dev/null; then echo "app still running: OK"; kill $APID; sleep 1; kill -9 $APID 2>/dev/null; else echo "APP NOT RUNNING"; fi
rm -rf "$OUT/config-$TAG"; cp -a "$HOME/.config" "$OUT/config-$TAG" 2>/dev/null || true
kill $XPID 2>/dev/null
kill "${DBUS_SESSION_BUS_PID:-0}" 2>/dev/null
echo "=== app log (filtered):"
grep -E -i "critical|warning|error|connect|auth|certif|rdp_|ssh_|vault|fail|disconn" "$OUT/app-$TAG.log" \
  | grep -v -E "GLib-GIO-DEBUG|Gdk-DEBUG|palette validation|vte_new|Gtk-DEBUG" | head -70
ls -la "$OUT" | grep "$TAG"
