# UniDeskBrowser

UniDeskBrowser is a remote desktop client for Linux written in GTK 3. It is a fork of
[Remmina](https://gitlab.com/Remmina/Remmina) 1.4.43 that keeps every connection in
browser-like windows with tabs.

[Русская версия](README.ru.md)

## Features

* **Protocols:** RDP (FreeRDP 3), SSH, SFTP and Telnet.
* **Windows and tabs like a browser:** several windows, each with its own tabs. The tabs of a
  window form a session; sessions are kept when a window closes and at exit, and the next start
  offers to continue where you left off. Tabs can be dragged between windows or out into a new
  one. Every window gets its own colour, so similar windows are easy to tell apart.
* **Home tab:** the connection list on the left, History on the right: the most used
  connections and the sessions of closed windows by day.
* **SFTP:** a two-panel file manager with Total Commander keys (F3 view, F4 edit, F5 copy,
  F6 move, F7 new folder, F8 delete…), transfers in the background, drag and drop to and from
  other file managers.
* **SSH ↔ SFTP:** the tab menu opens SFTP from an SSH tab and a terminal from an SFTP tab, to
  the same server with the same credentials.
* **Credentials:** profiles live in one database, passwords in an encrypted vault (with an
  optional primary password) or in the system keyring. Remmina profiles are imported on the
  first start.
* Dark theme; English and Russian user interface; the window scale adapts itself under XWayland.

## Differences from Remmina

* Application id `uz.core.Unidesk`, executable `unidesk`, plugins `unidesk-plugin-*`.
* VNC, SPICE, X2Go, EXEC, WWW and Python plugins, the online services, kiosk mode and Avahi
  were removed; a Telnet protocol was added.
* User directories: settings in `~/.config/unidesk/`, data and the connection database in
  `~/.local/share/unidesk/`, cache in `~/.cache/unidesk/`. The Remmina directories are only read,
  once, to import the profiles.

## Building

The project builds with CMake. The build dependencies and their versions (GTK 3, FreeRDP 3,
libssh, VTE, libsodium, libsecret, FUSE 3…) are listed in
[`packaging/appimage/Dockerfile`](packaging/appimage/Dockerfile).

A portable AppImage (glibc 2.35 or newer) is built in Docker:

```bash
bash packaging/appimage/build-image.sh    # once: the build image
bash packaging/appimage/build.sh          # the AppImage, in ../unidesk-build/out/
```

See [`packaging/appimage/README.md`](packaging/appimage/README.md) (in Russian) for the tests and
options. The changes of each version are in [`RELEASES.md`](RELEASES.md).

## License

Free software under the GNU General Public License, version 2 or later: see [COPYING](COPYING)
and [LICENSE](LICENSE).

UniDeskBrowser © 2026 CoRE technologies. Based on Remmina, originally written by Vic Lee and
maintained by Antenore Gatta, Giovanni Panozzo, Hiroyuki Tanaka, Sunil Bhat and many others: their
copyright notices are kept in the source files; see [AUTHORS](AUTHORS) and [THANKS](THANKS.md).
