#!/bin/bash
# Host-side wrapper: builds Unidesk and packages the AppImage inside the `unidesk-build` container.
# Usage: bash packaging/appimage/build.sh [extra cmake args]
# Environment:
#   UNIDESK_WORK   work directory for build/, AppDir/, out/ (default: ../unidesk-build next to the repository)
#   DOCKER         docker command (default: docker; use "sudo docker" when needed)
#   SKIP_PACKAGE=1 compile only, no AppImage
#   VERSION_STR    version suffix of the AppImage file name (default: version from CMakeLists.txt)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${UNIDESK_WORK:-$ROOT/../unidesk-build}
mkdir -p "$WORK/out" "$WORK/build"
WORK=$(cd "$WORK" && pwd)
DOCKER=${DOCKER:-docker}
$DOCKER run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -e SKIP_PACKAGE="${SKIP_PACKAGE:-0}" -e VERSION_STR="${VERSION_STR:-}" \
  -e APP_BIN="${APP_BIN:-unidesk}" -e APP_PLUGIN_DIR="${APP_PLUGIN_DIR:-unidesk/plugins}" \
  -e DESKTOP="${DESKTOP:-uz.core.Unidesk.desktop}" -e ICON_NAME="${ICON_NAME:-uz.core.Unidesk}" \
  -v "$WORK":/work -v "$ROOT":/work/src -v "$ROOT/packaging/appimage":/work/scripts:ro \
  -w /work unidesk-build bash /work/scripts/build-appimage.sh "$@"
