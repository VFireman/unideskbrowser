# icons/remmina - The GTK+ Remote Desktop Client
#
# Copyright (C) 2011 Marc-Andre Moreau
# Copyright (C) 2016-2023 Antenore Gatta, Giovanni Panozzo
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.
#
# In addition, as a special exception, the copyright holders give
# permission to link the code of portions of this program with the
# OpenSSL library under certain conditions as described in each
# individual source file, and distribute linked combinations
# including the two.
# You must obey the GNU General Public License in all respects
# for all of the code used other than OpenSSL. If you modify
# file(s) with this exception, you may extend this exception to your
# version of the file(s), but you are not obligated to do so. If you
# do not wish to do so, delete this exception statement from your
# version. If you delete this exception statement from all source
# files in the program, then also delete it here.


set(APPICONSCALE_ACTIONS_DIR "${REMMINA_DATADIR}/icons/hicolor/scalable/actions")
set(APPICONSCALE_EMBLEMS_DIR "${REMMINA_DATADIR}/icons/hicolor/scalable/emblems")

set(APPICONSCALE_ACTIONS_DATA
    scalable/actions/uz.core.Unidesk-camera-photo-symbolic.svg
    scalable/actions/uz.core.Unidesk-connect-symbolic.svg
    scalable/actions/uz.core.Unidesk-disconnect-symbolic.svg
    scalable/actions/uz.core.Unidesk-document-save-symbolic.svg
    scalable/actions/uz.core.Unidesk-document-send-symbolic.svg
    scalable/actions/uz.core.Unidesk-duplicate-symbolic.svg
    scalable/actions/uz.core.Unidesk-dynres-symbolic.svg
    scalable/actions/uz.core.Unidesk-fit-window-symbolic.svg
    scalable/actions/uz.core.Unidesk-fullscreen-symbolic.svg
    scalable/actions/uz.core.Unidesk-multi-monitor-symbolic.svg
    scalable/actions/uz.core.Unidesk-go-bottom-symbolic.svg
    scalable/actions/uz.core.Unidesk-keyboard-symbolic.svg
    scalable/actions/uz.core.Unidesk-pan-down-symbolic.svg
    scalable/actions/uz.core.Unidesk-pan-up-symbolic.svg
    scalable/actions/uz.core.Unidesk-pin-down-symbolic.svg
    scalable/actions/uz.core.Unidesk-pin-up-symbolic.svg
    scalable/actions/uz.core.Unidesk-preferences-system-symbolic.svg
    scalable/actions/uz.core.Unidesk-reconnect-symbolic.svg
    scalable/actions/uz.core.Unidesk-scale-symbolic.svg
    scalable/actions/uz.core.Unidesk-switch-page-symbolic.svg
    scalable/actions/uz.core.Unidesk-system-run-symbolic.svg
    scalable/actions/uz.core.Unidesk-password-reveal-symbolic.svg
    scalable/actions/uz.core.Unidesk-password-conceal-symbolic.svg
    scalable/actions/uz.core.Unidesk-document-properties-symbolic.svg
    scalable/actions/uz.core.Unidesk-go-home-symbolic.svg
    scalable/actions/uz.core.Unidesk-insert-object-symbolic.svg
    scalable/actions/uz.core.Unidesk-sidebar-symbolic.svg
    )

set(APPICONSCALE_EMBLEMS_DATA
    scalable/emblems/uz.core.Unidesk-sftp-symbolic.svg
    scalable/emblems/uz.core.Unidesk-ssh-symbolic.svg
    scalable/emblems/uz.core.Unidesk-telnet-symbolic.svg
    scalable/emblems/uz.core.Unidesk-status-red.svg
    scalable/emblems/uz.core.Unidesk-status-grey.svg
    scalable/emblems/uz.core.Unidesk-status-green.svg
    )

install(FILES ${APPICONSCALE_ACTIONS_DATA} DESTINATION ${APPICONSCALE_ACTIONS_DIR})
install(FILES ${APPICONSCALE_EMBLEMS_DATA} DESTINATION ${APPICONSCALE_EMBLEMS_DIR})

if(WITH_ICON_CACHE)
    gtk_update_icon_cache("${REMMINA_DATADIR}/icons/hicolor")
endif()
