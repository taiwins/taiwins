#cmake for libweston
#
#this cmake script is still adequate unitl weston-7, in which they changed again
#the interface, re-arranged header files.

find_package(PkgConfig)

pkg_search_module(Libweston REQUIRED libweston-${Libweston_FIND_VERSION})
pkg_search_module(LibwestonDesktop REQUIRED libweston-desktop-${Libweston_FIND_VERSION})

find_library(Libweston_LOCATION
  NAMES weston-${Libweston_FIND_VERSION}
  HINTS ${Libweston_LIBRARY_DIRS} ${Libweston_LIBDIR})

find_library(LibwestonDesktop_LOCATION
  NAMES weston-desktop-${Libweston_FIND_VERSION}
  HINTS  ${LibwestonDestkop_LIBRARY_DIRS} ${LibwestonDesktop_LIBDIR})

set(LibwestonDesktop_LIBRARY_DIRS ${LibwestonDesktop_LIBDIR} ${LibwestonDesktop_LIBRARY_DIRS})
set(Libweston_LIBRARY_DIRS ${Libweston_LIBDIR} ${Libweston_LIBRARY_DIRS})
set(Libweston_INCLUDE_DIRS ${Libweston_INCLUDEDIR} ${Libweston_INCLUDE_DIRS})
set(LibwestonDesktop_INCLUDE_DIRS ${LibwestonDesktop_INCLUDEDIR} ${LibwestonDesktop_INCLUDE_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBWESTON DEFAULT_MSG
  Libweston_LOCATION LibwestonDesktop_LOCATION
  Libweston_INCLUDE_DIRS  LibwestonDesktop_INCLUDE_DIRS
  Libweston_LIBRARIES LibwestonDesktop_LIBRARIES)

mark_as_advanced(
  Libweston_LOCATION LibwestonDesktop_LOCATION
  Libweston_INCLUDE_DIRS  LibwestonDesktop_INCLUDE_DIRS
  Libweston_LIBRARIES LibwestonDesktop_LIBRARIES)

if(LIBWESTON_FOUND AND NOT TARGET Libweston::Libweston)

  add_library(Libweston::Libweston UNKNOWN IMPORTED)
  set_target_properties(Libweston::Libweston PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    INTERFACE_LINK_DIRECTORIES "${Libweston_LIBRARY_DIRS}"
    INTERFACE_LINK_LIBRARIES "${Libweston_LIBRARIES}"
    IMPORTED_LOCATION "${Libweston_LOCATION}"
    INTERFACE_INCLUDE_DIRECTORIES "${Libweston_INCLUDE_DIRS}")

endif()

if(LIBWESTON_FOUND AND NOT TARGET Libweston::Desktop)

  add_library(Libweston::Desktop UNKNOWN IMPORTED)
  set_target_properties(Libweston::Desktop PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    INTERFACE_LINK_DIRECTORIES  "${LibwestonDesktop_LIBRARY_DIRS}"
    INTERFACE_LINK_LIBRARIES "${LibwestonDesktop_LIBRARIES}"
    IMPORTED_LOCATION "${LibwestonDesktop_LOCATION}"
    INTERFACE_INCLUDE_DIRECTORIES "${LibwestonDesktop_INCLUDE_DIRS}")

endif()
