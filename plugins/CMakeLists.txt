# Remmina - The GTK+ Remote Desktop Client
#
# Copyright (C) 2011 Marc-Andre Moreau
# Copyright (C) 2014-2015 Antenore Gatta, Fabio Castelli, Giovanni Panozzo
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

# Needed FreeRDP version to build freerdp plugin


include_directories(${CMAKE_SOURCE_DIR}/plugins)

set(APPICONSCALE_EMBLEMS_DIR "${REMMINA_DATADIR}/icons/hicolor/scalable/emblems")

set(REMMINA_COMMON_INCLUDE_DIRS ${GTK3_INCLUDE_DIRS})
set(REMMINA_COMMON_LIBRARIES ${GTK_LIBRARIES} ${Intl_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

find_suggested_package(LIBSSH)
if(LIBSSH_FOUND)
    add_definitions(-DHAVE_LIBSSH)
    include_directories(SYSTEM ${SSH_INCLUDE_DIRS})
    target_link_libraries(unidesk ${SSH_LIBRARIES})
endif()

option(WITH_FREERDP3 "Compile the RDP plugin using symbols from FreeRDP3" OFF)
if (WITH_FREERDP3)
    message(STATUS "Using FreeRDP3")
    set(FREERDP_REQUIRED_VERSION 3)
    set(FREERDP_REQUIRED_MAJOR 3)
    set(FREERDP_REQUIRED_MINOR 0)
    set(FREERDP_REQUIRED_REVISION 0)
else()
    message(STATUS "Using FreeRDP2")
    set(FREERDP_REQUIRED_VERSION 2)
    set(FREERDP_REQUIRED_MAJOR 2)
    set(FREERDP_REQUIRED_MINOR 0)
    set(FREERDP_REQUIRED_REVISION 0)
endif()

find_package(WinPR ${FREERDP_REQUIRED_VERSION} )
find_package(FreeRDP ${FREERDP_REQUIRED_VERSION} )
find_package(FreeRDP-Client ${FREERDP_REQUIRED_VERSION} )


if (WinPR_FOUND AND FreeRDP_FOUND AND FreeRDP-Client_FOUND)
    message(STATUS "Enabling RDP plugin")
    add_subdirectory(rdp)
else()
    message(STATUS "Disabling RDP plugin.")
endif()

option(WITH_KF5WALLET "Building KF5WALLET plugin" OFF)
if(WITH_KF5WALLET)
    CheckHasModule(KF5Wallet)
    if(HAS_MODULE_KF5Wallet)
        message(STATUS "Enabling KDE Wallet plugin.")
        add_definitions(-DWITH_KF5WALLET)
        add_subdirectory(kwallet)
    else()
        message(FATAL_ERROR "libKF5wallet not found but requested")
    endif()
endif()
