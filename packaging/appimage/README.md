# Сборка AppImage UniDeskBrowser

AppImage собирается внутри docker-контейнера на базе Ubuntu 22.04 (glibc 2.35), поэтому запускается на дистрибутивах Linux с glibc 2.35 и новее (Ubuntu 22.04 и новее, Debian 12 и новее, Arch и другие).

## Файлы

| Файл | Где выполняется | Назначение |
|---|---|---|
| `Dockerfile` | хост | образ `unidesk-build`: инструменты сборки, libssh 0.11.3 и FreeRDP 3.31.1 в `/usr/local`, linuxdeploy + GTK-плагин в `/opt/linuxdeploy`, Xvfb для проверок |
| `build-image.sh` | хост | собирает образ (`DOCKER="sudo docker"`, если пользователь не в группе docker) |
| `build.sh` | хост | запускает контейнер и сборку; результат в `$UNIDESK_WORK/out/Unidesk-<версия>-x86_64.AppImage` |
| `build-appimage.sh` | контейнер | cmake + ninja, установка в AppDir, linuxdeploy, темы иконок, собственный `AppRun` (тёмная тема), упаковка |
| `run-in-container.sh` | хост | выполнить произвольную команду в контейнере с теми же каталогами |
| `smoke-test.sh` | контейнер | запуск AppImage под Xvfb на N секунд, скриншот и журнал в `/work/out/shots/` |
| `live-test.sh` | контейнер | живое подключение к реальному RDP- или SSH-серверу под Xvfb: скриншоты на 8, 18 с и в конце, журнал и копия `~/.config` в `/work/out/shots/` |
| `restart-test.sh` | контейнер | три запуска подряд с одним и тем же `~/.config` (проверка, что сохранённые `vault.db`/`unidesk.pref` не ломают старт) |

Рабочий каталог (`$UNIDESK_WORK`, по умолчанию `../unidesk-build` рядом с репозиторием) монтируется как `/work`, репозиторий — как `/work/src`, этот каталог — как `/work/scripts`.

## Порядок

```bash
bash packaging/appimage/build-image.sh                 # один раз, 15–30 минут
bash packaging/appimage/build.sh                       # сборка и упаковка
SKIP_PACKAGE=1 bash packaging/appimage/build.sh        # только компиляция
VERSION_STR=dev3 bash packaging/appimage/build.sh      # своё имя файла
```

Проверка под Xvfb (скриншот `shot-01.png` и журнал `app-01.log` появятся в `$UNIDESK_WORK/out/shots/`):

```bash
bash packaging/appimage/run-in-container.sh "TAG=01 bash /work/scripts/smoke-test.sh /work/out/Unidesk-dev3-x86_64.AppImage 10"
bash packaging/appimage/run-in-container.sh "TAG=02 SEED_DIR=/work/seed bash /work/scripts/smoke-test.sh /work/out/Unidesk-dev3-x86_64.AppImage 12 -c db:0123456789abcdef"
```

`SEED_DIR` — каталог, содержимое которого копируется в `~/.config` тестового пользователя (например `unidesk/connections.db` с профилями; формат базы описан в начале `src/remmina_db.c`).

Живые проверки с реальными серверами (контейнер запускать с `--network host`: из bridge-сети докера соседние машины могут быть недоступны). Адреса, имя и пароль — ваших тестовых серверов:

```bash
docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp -e TAG=live-rdp \
  -v "$WORK":/work -v "$ROOT/packaging/appimage":/work/scripts:ro -w /work unidesk-build \
  bash /work/scripts/live-test.sh /work/out/Unidesk-dev7-x86_64.AppImage rdp <rdp-host> <user> '<password>' 35
docker run ... -e TAG=live-ssh -e KNOWN_HOSTS="<ssh-host> ssh-ed25519 AAAA..." ... \
  bash /work/scripts/live-test.sh /work/out/Unidesk-dev7-x86_64.AppImage ssh <ssh-host> <user> '<password>' 20
docker run ... bash /work/scripts/restart-test.sh /work/out/Unidesk-dev7-x86_64.AppImage /work/out/shots/config-live-rdp
```

Как `live-test.sh` обходит интерактивные запросы: для RDP сначала на 12 с запускается `xfreerdp /cert:tofu` в том же `HOME`, чтобы сертификат сервера лёг в `~/.config/freerdp/server/<host>_<port>.pem` (Unidesk использует то же хранилище FreeRDP и не задаёт вопрос «Accept certificate?»); для SSH ключ хоста кладётся в `~/.ssh/known_hosts` из `KNOWN_HOSTS`; пароль шифруется `unidesk --encrypt-password` с секретом из только что созданного `unidesk.pref` и передаётся в URI `proto://user:encrypted@host` (открытый короткий пароль в URI не годится: `remmina_crypt_decrypt` превращает строку вроде `1` в пустую).

## Что делает AppRun

linuxdeploy генерирует `AppRun`, который подключает только хук GTK-плагина; `build-appimage.sh` заменяет его своим: выполняются все `apprun-hooks/*.sh`, переменная `APPIMAGE_GTK_THEME=Adwaita:dark` задаётся до хука GTK, а хук `zz-unidesk.sh` выставляет `UNIDESK_APPDIR` (по ней программа находит плагины и данные) и `GTK_THEME=Adwaita:dark`.
