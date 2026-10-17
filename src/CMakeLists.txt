# src/remmina - The GTK+ Remote Desktop Client
#
# Copyright (C) 2011 Marc-Andre Moreau
# Copyright (C) 2014-2015 Antenore Gatta, Fabio Castelli, Giovanni Panozzo
# Copyright (C) 2016-2023 Antenore Gatta, Giovanni Panozzo
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
# Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
# In addition, as a special exception, the copyright holders give permission to
# link the code of portions of this program with the OpenSSL library under
# certain conditions as described in each individual source file, and distribute
# linked combinations including the two. You must obey the GNU General Public
# License in all respects for all of the code used other than OpenSSL. If you
# modify file(s) with this exception, you may extend this exception to your
# version of the file(s), but you are not obligated to do so. If you do not wish
# to do so, delete this exception statement from your version. If you delete
# this exception statement from all source files in the program, then also
# delete it here.

list(
  APPEND
  REMMINA_SRCS
  "remmina_about.c"
  "remmina_about.h"
  "remmina_applet_menu.c"
  "remmina_applet_menu.h"
  "remmina_applet_menu_item.c"
  "remmina_applet_menu_item.h"
  "remmina.c"
  "remmina.h"
  "remmina_chat_window.c"
  "remmina_chat_window.h"
  "remmina_crypt.c"
  "remmina_crypt.h"
  "remmina_exec.c"
  "remmina_exec.h"
  "remmina_file.c"
  "remmina_file_editor.c"
  "remmina_file_editor.h"
  "remmina_file.h"
  "remmina_file_manager.c"
  "remmina_file_manager.h"
  "remmina_ftp_client.c"
  "remmina_ftp_client.h"
  "remmina_ftp_dialogs.c"
  "remmina_ftp_dialogs.h"
  "remmina_ftp_fs.c"
  "remmina_ftp_fs.h"
  "remmina_ftp_job.c"
  "remmina_ftp_job.h"
  "remmina_ftp_panel.c"
  "remmina_ftp_panel.h"
  "remmina_icon.c"
  "remmina_icon.h"
  "remmina_key_chooser.c"
  "remmina_key_chooser.h"
  "remmina_log.c"
  "remmina_log.h"
  "remmina_main.c"
  "remmina_main.h"
  "remmina_marshals.c"
  "remmina_marshals.h"
  "remmina_marshals.list"
  "remmina_masterthread_exec.c"
  "remmina_masterthread_exec.h"
  "remmina_message_panel.c"
  "remmina_message_panel.h"
  "remmina_plugin_manager.c"
  "remmina_plugin_manager.h"
  "remmina_plugin_native.c"
  "remmina_plugin_native.h"
  "remmina_ext_exec.c"
  "remmina_ext_exec.h"
  "remmina_passwd.c"
  "remmina_passwd.h"
  "remmina_pref.c"
  "remmina_pref_dialog.c"
  "remmina_pref_dialog.h"
  "remmina_pref.h"
  "remmina_protocol_widget.c"
  "remmina_protocol_widget.h"
  "remmina_public.c"
  "remmina_public.h"
  "remmina_scrolled_viewport.c"
  "remmina_scrolled_viewport.h"
  "remmina_sftp_client.c"
  "remmina_sftp_client.h"
  "remmina_sftp_plugin.c"
  "remmina_sftp_plugin.h"
  "remmina_sodium.c"
  "remmina_sodium.h"
  "remmina_ssh.c"
  "remmina_ssh.h"
  "remmina_ssh_plugin.c"
  "remmina_ssh_plugin.h"
  "remmina_string_array.c"
  "remmina_string_array.h"
  "remmina_string_list.c"
  "remmina_string_list.h"
  "remmina_unlock.c"
  "remmina_unlock.h"
  "remmina_utils.c"
  "remmina_utils.h"
  "remmina_widget_pool.c"
  "remmina_widget_pool.h"
  "remmina_external_tools.c"
  "remmina_external_tools.h"
  "remmina_sysinfo.h"
  "remmina_sysinfo.c"
  "remmina_theme.h"
  "remmina_theme.c"
  "rcw_platform_kde.c"
  "rcw_platform_kde.h"
  "rcw_platform.c"
  "rcw_platform.h"
  "rcw.c"
  "rcw.h"
  "remmina_mpchange.c"
  "remmina_mpchange.h"
  "remmina_telnet_plugin.c"
  "remmina_telnet_plugin.h"
  "libtelnet.c"
  "libtelnet.h"
  "remmina_runtime.c"
  "remmina_runtime.h"
  "remmina_vault.c"
  "remmina_vault.h"
  "remmina_db.c"
  "remmina_db.h"
  "remmina_session.c"
  "remmina_session.h"
  "remmina_scale.c"
  "remmina_scale.h"
  "resources.c")

set(RESOURCE_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_about.glade
	${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_file_editor.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_key_chooser.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_main.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_mpc.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_passwd.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_preferences.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_search.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_search_popover.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_spinner.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_string_list.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/ui/remmina_unlock.glade
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/style/unidesk-dark.css
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/style/unidesk-fm.css
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/style/unidesk-accent.css
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/style/unidesk-history.css)
message(DEBUG "Source list set to: " ${REMMINA_SRCS})

compile_gresources(
  RESOURCE_FILE
  XML_OUT
  TYPE
  EMBED_C
  PREFIX
  /org/unidesk/Unidesk/src
  SOURCE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}
  RESOURCES
  ${RESOURCE_LIST})

add_custom_target(resource DEPENDS ${RESOURCE_FILE})
add_executable(unidesk ${REMMINA_SRCS} ${RESOURCE_FILE})
add_dependencies(unidesk resource)
target_link_libraries(unidesk ${GTK_LIBRARIES} -rdynamic)

if(HAVE_KDE_OUTPUT_ORDER_V1)
	target_sources(unidesk PRIVATE ${KDE_OUTPKODE_GENERATED_C})
endif()

if(WITH_MANPAGES)
  install(FILES unidesk.1 DESTINATION ${CMAKE_INSTALL_FULL_MANDIR}/man1)
endif()

target_link_libraries(unidesk ${CMAKE_THREAD_LIBS_INIT})

if(Intl_FOUND)
  message(STATUS "${CMAKE_SYSTEM_NAME} detected, building with Intl")
  include_directories(SYSTEM ${Intl_INCLUDE_DIRS})
  target_link_libraries(unidesk ${Intl_LIBRARIES})
endif()

find_suggested_package(LIBSSH)
if(LIBSSH_FOUND)
  add_definitions(-DHAVE_LIBSSH)
  include_directories(SYSTEM ${LIBSSH_INCLUDE_DIRS})
  target_link_libraries(unidesk ${LIBSSH_LIBRARIES})
endif()

# The SFTP tab lends server files dragged out of it to other programs through a FUSE file system
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(FUSE3 fuse3)
endif()
if(LIBSSH_FOUND AND FUSE3_FOUND)
  add_definitions(-DHAVE_FUSE3)
  target_sources(unidesk PRIVATE remmina_ftp_fuse.c remmina_ftp_fuse.h)
  include_directories(SYSTEM ${FUSE3_INCLUDE_DIRS})
  target_link_libraries(unidesk ${FUSE3_LIBRARIES})
endif()

# remmina_scale.c fits GDK_SCALE to the screens when drawing through XWayland: the compositor's
# outputs (xdg-output for a fractional scale) against the X screen (Xlib)
if(wayland-client_FOUND AND PKG_CONFIG_FOUND)
  pkg_check_modules(X11LIB x11)
  pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
  find_program(WAYLAND_SCANNER_BIN wayland-scanner)
endif()
if(wayland-client_FOUND AND X11LIB_FOUND)
  add_definitions(-DHAVE_XWAYLAND_SCALE)
  include_directories(SYSTEM ${X11LIB_INCLUDE_DIRS})
  target_link_libraries(unidesk ${X11LIB_LIBRARIES})
  set(XDG_OUTPUT_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-output/xdg-output-unstable-v1.xml")
  if(WAYLAND_SCANNER_BIN AND WAYLAND_PROTOCOLS_DIR AND EXISTS "${XDG_OUTPUT_XML}")
    set(XDG_OUTPUT_H "${CMAKE_CURRENT_BINARY_DIR}/xdg-output-unstable-v1-client-protocol.h")
    set(XDG_OUTPUT_C "${CMAKE_CURRENT_BINARY_DIR}/xdg-output-unstable-v1-protocol.c")
    add_custom_command(
      OUTPUT "${XDG_OUTPUT_H}" "${XDG_OUTPUT_C}"
      COMMAND ${WAYLAND_SCANNER_BIN} client-header "${XDG_OUTPUT_XML}" "${XDG_OUTPUT_H}"
      COMMAND ${WAYLAND_SCANNER_BIN} private-code "${XDG_OUTPUT_XML}" "${XDG_OUTPUT_C}"
      DEPENDS "${XDG_OUTPUT_XML}")
    target_sources(unidesk PRIVATE "${XDG_OUTPUT_H}" "${XDG_OUTPUT_C}")
    target_include_directories(unidesk PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    add_definitions(-DHAVE_XDG_OUTPUT)
    message(STATUS "XWayland scale: xdg-output from ${XDG_OUTPUT_XML}")
  else()
    message(STATUS "XWayland scale: no xdg-output, whole scales only")
  endif()
endif()

if(GCRYPT_FOUND)
  include_directories(SYSTEM ${GCRYPT_INCLUDE_DIRS})
  target_link_libraries(unidesk ${GCRYPT_LIBRARIES})
endif()

option(WITH_VTE "Build with support for VTE" ON)
if(GTK3_FOUND AND WITH_VTE)
  set(_VTE_VERSION_NUMS 2.91 2.90)
  foreach(__VTE_VERSION ${_VTE_VERSION_NUMS})
    set(_VTE_VERSION_NUM ${__VTE_VERSION})
    find_suggested_package(VTE)
    if(VTE_FOUND)
      break()
    endif()
    message(STATUS "VTE ${__VTE_VERSION} not found")
  endforeach()
elseif(WITH_VTE)
  set(_VTE_VERSION_NUM)
  find_package(VTE)
endif()

if(VTE_FOUND)
  add_definitions(-DHAVE_LIBVTE)
  include_directories(SYSTEM ${VTE_INCLUDE_DIRS})
  target_link_libraries(unidesk ${VTE_LIBRARIES})
endif()

if(sodium_FOUND)
  message(STATUS "Sodium found")
  message(STATUS "Sodium include dirs ${sodium_INCLUDE_DIR}")
  include_directories(SYSTEM ${sodium_INCLUDE_DIR})
  target_link_libraries(unidesk sodium)
endif()

# Default to OFF on macOS since we have native status item support
if(APPLE)
    option(HAVE_LIBAPPINDICATOR "Build with support for status icon. Appindicator is required" OFF)
else()
    option(HAVE_LIBAPPINDICATOR "Build with support for status icon. Appindicator is required" ON)
endif()
if(GTK3_FOUND)
    if(HAVE_LIBAPPINDICATOR)
        find_required_package(APPINDICATOR)
        if(APPINDICATOR_FOUND)
            add_definitions(-DHAVE_LIBAPPINDICATOR)
            include_directories(SYSTEM ${APPINDICATOR_INCLUDE_DIRS})
            target_link_libraries(unidesk ${APPINDICATOR_LIBRARIES})
            message(STATUS "AppIndicator library dirs ${APPINDICATOR_LIBRARIES}")
            message(STATUS "AppIndicator include dirs ${APPINDICATOR_INCLUDE_DIRS}")
        else()
            message(FATAL_ERROR "AppIndicator not found")
        endif()
    endif()
endif()

find_required_package(PCRE2)
if(NOT PCRE2_FOUND)
  message(FATAL_ERROR "libpcre2 library not found")
endif()
include_directories(SYSTEM ${PCRE2_INCLUDE_DIRS})

add_subdirectory(external_tools)

install(TARGETS unidesk DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
  DIRECTORY include/remmina/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/remmina
  FILES_MATCHING
  PATTERN "*.h")

add_definitions(-DG_LOG_DOMAIN="unidesk")
