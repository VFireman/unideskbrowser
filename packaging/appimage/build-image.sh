#!/bin/bash
# Builds the `unidesk-build` docker image (Ubuntu 22.04 + libssh + FreeRDP + linuxdeploy) and the
# `unidesk-test` image on top of it (UI and live test tools).
# Usage: bash packaging/appimage/build-image.sh      (DOCKER="sudo docker" if your user is not in the docker group)
set -euo pipefail
cd "$(dirname "$0")"
DOCKER=${DOCKER:-docker}
echo "=== image build started: $(date)"
$DOCKER build -t unidesk-build .
$DOCKER build -t unidesk-test -f Dockerfile.test .
echo "=== image build finished: $(date)"
