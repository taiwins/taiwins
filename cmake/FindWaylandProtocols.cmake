#.rst:
# Findwlproto
# -------
#
# Find wayland protocol description files
#
# Try to find wayland protocol files. The following values are defined
#
# ::
#
#   WLPROTO_FOUND         - True if wayland protocol files are available
#   WLPROTO_PATH          - Path to wayland protocol files
#
#=============================================================================
# Copyright (c) 2015 Jari Vetoniemi
#
# Distributed under the OSI-approved BSD License (the "License");
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================

include(FeatureSummary)
set_package_properties(wlproto PROPERTIES
   URL "https://cgit.freedesktop.org/wayland/wayland-protocols"
   DESCRIPTION "Wayland protocol development")

unset(WLPROTO_PATH)

find_package(PkgConfig)
pkg_check_modules(wlproto QUIET wayland-protocols>=${WaylandProtocols_FIND_VERSION})
execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pkgdatadir wayland-protocols
  OUTPUT_VARIABLE WLPROTO_PATH OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _pkgconfig_failed)

if (_pkgconfig_failed)
  message(FATAL_ERROR "Missing wayland-protocols pkgdatadir")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WaylandProtocols DEFAULT_MSG WLPROTO_PATH)
mark_as_advanced(WLPROTO_PATH)

if (WLPROTO_PATH AND NOT TARGET WaylandProtocols)
  add_library(WaylandProtocols INTERFACE)
  set_target_properties(WaylandProtocols PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${WLPROTO_PATH}"
    )
endif()
