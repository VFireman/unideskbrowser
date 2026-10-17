#!/bin/bash
# Builds UniDeskBrowser and packages it as an AppImage. Runs INSIDE the unidesk-build container.
# /work is the work directory of build.sh on the host: src/ (sources), build/, AppDir/, out/.
set -euo pipefail
export APPIMAGE_EXTRACT_AND_RUN=1
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}
export LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH:-}

SRC=${SRC:-/work/src}
BUILD=${BUILD:-/work/build}
APPDIR=${APPDIR:-/work/AppDir}
OUT=${OUT:-/work/out}
SKIP_PACKAGE=${SKIP_PACKAGE:-0}      # 1 = stop after ninja (compile check only)
APP_BIN=${APP_BIN:-unidesk}          # name of the installed executable
APP_PLUGIN_DIR=${APP_PLUGIN_DIR:-unidesk/plugins}
DESKTOP=${DESKTOP:-uz.core.Unidesk.desktop}
ICON_NAME=${ICON_NAME:-uz.core.Unidesk}

echo "=== configure $(date)"
rm -rf "$APPDIR"
mkdir -p "$OUT" "$BUILD"
# The sources are synced without .git; the sync step leaves the revision in .git-revision
if [ -z "${REMMINA_GIT_REVISION:-}" ] && [ -s "$SRC/.git-revision" ]; then
  set -- "$@" "-DREMMINA_GIT_REVISION=$(tr -d '[:space:]' < "$SRC/.git-revision")"
fi
# The build name (VERSION_STR, e.g. dev18) is shown in About; always passed, so a cached value never outlives its build
set -- "$@" "-DUNIDESK_BUILD=${VERSION_STR:-}"
cmake -S "$SRC" -B "$BUILD" -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DWITH_FREERDP3=ON -DWITH_WWW=OFF -DWITH_PYTHONLIBS=OFF -DWITH_NEWS=OFF -DWITH_STATS=OFF -DWITH_TIP=OFF \
  -DWITH_MANPAGES=OFF -DWITH_ICON_CACHE=OFF -DWITH_UPDATE_DESKTOP_DB=OFF -DWITH_KF5WALLET=OFF \
  -DWITH_GVNC=OFF -DWITH_X2GO=OFF -DWITH_EXAMPLES=OFF -DWITH_TRANSLATIONS=ON "$@"

echo "=== build $(date)"
ninja -C "$BUILD"
if [ "$SKIP_PACKAGE" = "1" ]; then echo "=== compile check done $(date)"; exit 0; fi

echo "=== install into AppDir $(date)"
DESTDIR="$APPDIR" ninja -C "$BUILD" install

echo "=== deploy with linuxdeploy $(date)"
DEPS_ARGS=()
while IFS= read -r f; do
  DEPS_ARGS+=(--deploy-deps-only "$f")
done < <(find "$APPDIR/usr/lib" -path "*/$APP_PLUGIN_DIR/*.so" 2>/dev/null)
# FreeRDP dynamic addins (if any were built as plugins) live under /usr/local/lib/freerdp3
if [ -d /usr/local/lib/freerdp3 ]; then
  mkdir -p "$APPDIR/usr/lib/freerdp3"
  cp -a /usr/local/lib/freerdp3/. "$APPDIR/usr/lib/freerdp3/"
  for f in "$APPDIR"/usr/lib/freerdp3/*.so; do
    [ -e "$f" ] && DEPS_ARGS+=(--deploy-deps-only "$f")
  done
fi
DESKTOP_FILE=$(ls "$APPDIR"/usr/share/applications/"$DESKTOP" 2>/dev/null || ls "$APPDIR"/usr/share/applications/*.desktop | head -1)
# linuxdeploy needs a bare executable name in Exec/TryExec (no absolute path)
sed -i -E "s|^Exec=.*|Exec=$APP_BIN %U|; s|^TryExec=.*|TryExec=$APP_BIN|" "$DESKTOP_FILE"
# secondary desktop files (file handler) must not reference an absolute path either
for f in "$APPDIR"/usr/share/applications/*.desktop; do
  sed -i -E "s|^Exec=/[^ ]*/([a-z]+)|Exec=\1|; s|^TryExec=/[^ ]*/([a-z]+)|TryExec=\1|" "$f"
done
ICON_FILE=$(ls "$APPDIR"/usr/share/icons/hicolor/scalable/apps/"$ICON_NAME".svg 2>/dev/null || ls "$APPDIR"/usr/share/icons/hicolor/256x256/apps/*.png | head -1)
export DEPLOY_GTK_VERSION=3
LINUXDEPLOY=${LINUXDEPLOY:-linuxdeploy-x86_64.AppImage}
LINUXDEPLOY_APPIMAGE=${LINUXDEPLOY_APPIMAGE:-linuxdeploy-plugin-appimage-x86_64.AppImage}
"$LINUXDEPLOY" --appdir "$APPDIR" --plugin gtk \
  --executable "$APPDIR/usr/bin/$APP_BIN" \
  --desktop-file "$DESKTOP_FILE" --icon-file "$ICON_FILE" \
  "${DEPS_ARGS[@]}"

echo "=== OpenSSL legacy provider $(date)"
# linuxdeploy bundles libcrypto.so.3, whose compiled-in module directory (/usr/lib/x86_64-linux-gnu/ossl-modules)
# does not exist on other distributions. Without the "legacy" provider there is no MD4/RC4, and NTLM (RDP NLA)
# fails on the target host: "OpenSSL LEGACY provider failed to load, no md4 support available".
# Ship the provider next to the bundled library and point OPENSSL_MODULES at it (see the AppRun hook below).
if [ -e "$APPDIR/usr/lib/libcrypto.so.3" ]; then
  OSSL_MODULES_SRC=$( (ls -d /usr/lib/*/ossl-modules /usr/lib/ossl-modules 2>/dev/null || true) | head -1)
  if [ -n "$OSSL_MODULES_SRC" ] && [ -e "$OSSL_MODULES_SRC/legacy.so" ]; then
    mkdir -p "$APPDIR/usr/lib/ossl-modules"
    cp -a "$OSSL_MODULES_SRC"/*.so "$APPDIR/usr/lib/ossl-modules/"
    ls "$APPDIR/usr/lib/ossl-modules/"
  else
    echo "WARNING: OpenSSL legacy provider not found; NTLM will not work on hosts without it" >&2
  fi
fi

echo "=== icon themes $(date)"
# hicolor index (our own icons live in hicolor) and the Adwaita icons used by GTK/our UI by name
mkdir -p "$APPDIR/usr/share/icons/hicolor"
cp -n /usr/share/icons/hicolor/index.theme "$APPDIR/usr/share/icons/hicolor/" 2>/dev/null || true
if [ -d /usr/share/icons/Adwaita ] && [ ! -d "$APPDIR/usr/share/icons/Adwaita" ]; then
  cp -a /usr/share/icons/Adwaita "$APPDIR/usr/share/icons/"
  rm -rf "$APPDIR/usr/share/icons/Adwaita/cursors"
fi

echo "=== GTK translation $(date)"
# GTK's own strings (dialog buttons, file chooser…) in Russian even on hosts without GTK translations:
# po/ru.po of the bundled GTK version (3.24.33); the application binds "gtk30" here (remmina.c)
if [ -s "$SRC/packaging/appimage/gtk30-ru.po" ]; then
  mkdir -p "$APPDIR/usr/share/locale/ru/LC_MESSAGES"
  msgfmt -o "$APPDIR/usr/share/locale/ru/LC_MESSAGES/gtk30.mo" "$SRC/packaging/appimage/gtk30-ru.po"
fi

echo "=== custom AppRun hooks $(date)"
# linuxdeploy's generated AppRun sources only the gtk plugin hook, so we write our own AppRun:
# it runs every hook in apprun-hooks/ (gtk plugin first, ours last) and forces the dark theme.
mkdir -p "$APPDIR/apprun-hooks"
cat > "$APPDIR/apprun-hooks/zz-unidesk.sh" <<'EOF'
export UNIDESK_APPDIR="$APPDIR"
export GTK_THEME="Adwaita:dark"
export XDG_DATA_DIRS="$APPDIR/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
# providers for the bundled libcrypto (legacy = MD4/RC4 for NTLM)
if [ -d "$APPDIR/usr/lib/ossl-modules" ]; then export OPENSSL_MODULES="$APPDIR/usr/lib/ossl-modules"; fi
EOF
if [ ! -e "$APPDIR/AppRun.wrapped" ]; then
  rm -f "$APPDIR/AppRun"
  ln -s "usr/bin/$APP_BIN" "$APPDIR/AppRun.wrapped"
fi
cat > "$APPDIR/AppRun" <<'EOF'
#! /usr/bin/env bash
# Unidesk AppRun: run every hook, then the real binary
set -e
this_dir="$(readlink -f "$(dirname "$0")")"
export APPIMAGE_GTK_THEME="Adwaita:dark"
for hook in "$this_dir"/apprun-hooks/*.sh; do
  [ -r "$hook" ] && source "$hook"
done
exec "$this_dir"/AppRun.wrapped "$@"
EOF
chmod +x "$APPDIR/AppRun"

echo "=== package $(date)"
# The project's version (the indented line inside project(), not cmake_minimum_required)
VERSION_STR=${VERSION_STR:-$(grep -m1 -oP '^\s+VERSION \K[0-9.]+' "$SRC/CMakeLists.txt" || echo 0)}
export LDAI_OUTPUT="$OUT/UniDeskBrowser-${VERSION_STR}-x86_64.AppImage"
# no update information: an empty value makes appimagetool fail ("not in a recognized format") after packaging
unset LDAI_UPDATE_INFORMATION
"$LINUXDEPLOY_APPIMAGE" --appdir "$APPDIR"
ls -la "$OUT"
echo "=== done $(date)"
