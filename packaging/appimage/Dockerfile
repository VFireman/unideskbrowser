# UniDeskBrowser build image: Ubuntu 22.04 (glibc 2.35) so the AppImage runs on distributions with glibc 2.35 or newer.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV APPIMAGE_EXTRACT_AND_RUN=1

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git pkg-config ca-certificates curl wget file patchelf \
    gettext intltool desktop-file-utils \
    libglib2.0-dev libglib2.0-bin libgtk-3-dev libvte-2.91-dev libgcrypt20-dev libsodium-dev libsecret-1-dev \
    libjson-glib-dev libcurl4-openssl-dev libayatana-appindicator3-dev libpcre2-dev libxkbfile-dev libssl-dev \
    libsoup2.4-dev librsvg2-common librsvg2-dev zlib1g-dev \
    libx11-dev libxext-dev libxinerama-dev libxcursor-dev libxdamage-dev libxv-dev libxrandr-dev libxi-dev \
    libxfixes-dev libxrender-dev libxkbcommon-dev libwayland-dev \
    libasound2-dev libpulse-dev libcups2-dev libjpeg-dev libcairo2-dev libicu-dev libkrb5-dev libusb-1.0-0-dev \
    libudev-dev libsystemd-dev libpam0g-dev uuid-dev libxml2-dev \
    libavcodec-dev libavutil-dev libswresample-dev libswscale-dev libpkcs11-helper1-dev libcjson-dev \
    xvfb x11-utils x11-apps imagemagick dbus-x11 fonts-dejavu-core adwaita-icon-theme \
    && rm -rf /var/lib/apt/lists/*

# libssh 0.11.3 (same version as the official Remmina flatpak)
RUN cd /tmp && wget -q https://www.libssh.org/files/0.11/libssh-0.11.3.tar.xz \
    && tar xf libssh-0.11.3.tar.xz && cd libssh-0.11.3 \
    && cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
       -DWITH_EXAMPLES=OFF -DBUILD_TESTING=OFF -DWITH_GSSAPI=ON -DWITH_ZLIB=ON \
    && ninja -C build && ninja -C build install && cd / && rm -rf /tmp/libssh-0.11.3*

# FUSE 3 for FreeRDP's clipboard file system: files copied in Windows are pasted here through it
RUN apt-get update && apt-get install -y --no-install-recommends libfuse3-dev \
    && rm -rf /var/lib/apt/lists/*

# FreeRDP 3.31.1 (same version as the official Remmina flatpak), with our clipboard file patch
COPY freerdp-cliprdr-file-fuse.patch /tmp/
RUN cd /tmp && git clone --depth 1 --branch 3.31.1 https://github.com/FreeRDP/FreeRDP.git \
    && cd FreeRDP && git apply /tmp/freerdp-cliprdr-file-fuse.patch \
    && cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
       -DWITH_SERVER=OFF -DWITH_SAMPLE=OFF -DWITH_CLIENT_SDL=OFF -DWITH_CLIENT_SDL2=OFF -DWITH_CLIENT_SDL3=OFF \
       -DWITH_X11=ON -DWITH_WAYLAND=OFF -DWITH_CLIENT_WAYLAND=OFF \
       -DBUILTIN_CHANNELS=ON -DWITH_CHANNELS=ON -DWITH_CLIENT_CHANNELS=ON \
       -DWITH_PULSE=ON -DWITH_ALSA=ON -DWITH_CUPS=ON -DWITH_FFMPEG=ON -DWITH_SWSCALE=ON -DWITH_DSP_FFMPEG=ON \
       -DWITH_OPENH264=OFF -DWITH_KRB5=ON -DWITH_FUSE=ON -DCHANNEL_URBDRC=OFF -DWITH_WEBVIEW=OFF \
       -DWITH_CAIRO=ON -DWITH_ICU=ON -DWITH_MANPAGES=OFF -DBUILD_TESTING=OFF -DWITH_PKCS11=ON \
       -DWITH_WINPR_TOOLS=ON -DWITH_CLIENT=ON \
    && ninja -C build && ninja -C build install && cd / && rm -rf /tmp/FreeRDP /tmp/freerdp-cliprdr-file-fuse.patch

RUN ldconfig

# AppImage tooling (run with APPIMAGE_EXTRACT_AND_RUN=1 because there is no FUSE in a container)
RUN mkdir -p /opt/linuxdeploy && cd /opt/linuxdeploy \
    && wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
    && wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-gtk/raw/master/linuxdeploy-plugin-gtk.sh \
    && wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-x86_64.AppImage \
    && chmod +x /opt/linuxdeploy/*
ENV PATH="/opt/linuxdeploy:${PATH}"
ENV PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/local/lib/x86_64-linux-gnu/pkgconfig"

WORKDIR /work
