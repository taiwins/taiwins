# - Find XKBCommon
# Once done, this will define
#
#   XKBCOMMON_FOUND - System has XKBCommon
#   XKBCOMMON_INCLUDE_DIRS - The XKBCommon include directories
#   XKBCOMMON_LIBRARIES - The libraries needed to use XKBCommon
#   XKBCOMMON_DEFINITIONS - Compiler switches required for using XKBCommon

find_package(PkgConfig)
pkg_check_modules(XKBCOMMON REQUIRED QUIET xkbcommon)
find_library(XKBCOMMON_LOCATION NAMES xkbcommon HINTS ${XKBCOMMON_LIBRARY_DIRS})
if(NOT XKBCOMMON_INCLUDE_DIRS)
  #TODO this is a bug, find_path doesn't work here
  set(XKBCOMMON_INCLUDE_DIRS ${XKBCOMMON_INCLUDEDIR})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(XKBCommon DEFAULT_MSG XKBCOMMON_LIBRARIES XKBCOMMON_INCLUDE_DIRS)
mark_as_advanced(XKBCOMMON_LIBRARIES XKBCOMMON_INCLUDE_DIRS)

if (XKBCOMMON_FOUND AND NOT TARGET XKBCommon::XKBCommon)
  add_library(XKBCommon::XKBCommon UNKNOWN IMPORTED)
  set_target_properties(XKBCommon::XKBCommon PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${XKBCOMMON_LOCATION}"
    INTERFACE_LINK_DIRECTORIES "${XKBCOMMON_LIBRARY_DIRS}"
    INTERFACE_LINK_LIBRARIES "${XKBCOMMON_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${XKBCOMMON_INCLUDE_DIRS}"
    )
endif()
