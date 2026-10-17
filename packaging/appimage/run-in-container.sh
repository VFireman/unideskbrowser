#!/bin/bash
# Runs an arbitrary shell command inside the `unidesk-build` container with the same mounts as build.sh
# (/work = work directory, /work/src = repository, /work/scripts = this directory).
# Usage: bash packaging/appimage/run-in-container.sh "<command>"
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${UNIDESK_WORK:-$ROOT/../unidesk-build}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
DOCKER=${DOCKER:-docker}
$DOCKER run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v /etc/passwd:/etc/passwd:ro -v /etc/group:/etc/group:ro \
  -v "$WORK":/work -v "$ROOT":/work/src -v "$ROOT/packaging/appimage":/work/scripts:ro \
  -w /work unidesk-build bash -c "$*"
