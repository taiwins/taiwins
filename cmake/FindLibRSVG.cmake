# This module defines
# Rsvg_FOUND
# Rsvg_INCLUDE_DIR
# Rsvg_LIBRARY_DIR
# Rsvg_LIBS
find_package(PkgConfig)
pkg_check_modules(pc_librsvg QUIET librsvg-2.0)

set(LibRSVG_INCLUDE_DIRS ${pc_librsvg_INCLUDE_DIRS})
set(LibRSVG_LIBRARIES ${pc_librsvg_LIBRARIES})

find_package_handle_standard_args(LibRSVG DEFAULT_MSG LibRSVG_INCLUDE_DIRS LibRSVG_LIBRARIES)

# if(LibRSVG_FOUND)
#   message(STATUS " found rsvg")
#   message(STATUS " - Includes: ${LibRSVG_INCLUDE_DIRS}")
#   message(STATUS " - Libraries: ${LibRSVG_LIBRARIES}")
# else(LibRSVG_FOUND)
#   message(FATAL_ERROR "Could not find rsvg installation.")
# endif(LibRSVG_FOUND)
