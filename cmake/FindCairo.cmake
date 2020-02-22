# - Try to find the cairo library
# Once done this will define
#
#  CAIRO_FOUND - system has cairo
#  CAIRO_INCLUDE_DIRS - the cairo include directory
#  CAIRO_LIBRARIES - Link these to use cairo
#
# Define CAIRO_MIN_VERSION for which version desired.
#
include(FindPackageHandleStandardArgs)
find_package(PkgConfig)
pkg_check_modules(CAIRO REQUIRED QUIET cairo)
find_library(CAIRO_LOCATION NAMES cairo HINTS ${CAIRO_LIBRARY_DIRS})
# Hide advanced variables from CMake GUIs
MARK_AS_ADVANCED(CAIRO_LIBRARIES CAIRO_INCLUDE_DIRS)
find_package_handle_standard_args(Cairo DEFAULT_MSG CAIRO_LIBRARIES CAIRO_INCLUDE_DIRS)

if (CAIRO_FOUND AND NOT TARGET Cairo::Cairo)
  add_library(Cairo::Cairo UNKNOWN IMPORTED)
  set_target_properties(Cairo::Cairo PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    INTERFACE_LINK_DIRECTORIES "${CAIRO_LIBRARY_DIRS}"
    INTERFACE_LINK_LIBRARIES "${CAIRO_LIBRARIES}"
    IMPORTED_LOCATION "${CAIRO_LOCATION}"
    INTERFACE_INCLUDE_DIRECTORIES "${CAIRO_INCLUDE_DIRS}"
    )
endif()
