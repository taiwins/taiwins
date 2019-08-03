#cmake for libweston
#
#this cmake script is still adequate unitl weston-7, in which they changed again
#the interface, re-arranged header files.

find_package(PkgConfig)

pkg_search_module(Libweston libweston-6 libweston-5 libweston-4 libweston>=4 REQUIRED)
pkg_search_module(Libweston-Desktop libweston-desktop-6 libweston-desktop-5 libweston-desktop-4 libweston-desktop>=4 REQUIRED)

find_path(LIBWESTON_INCLUDE_DIR compositor.h
  HINTS ${Libweston_INCLUDE_DIRS})

find_path(LIBWESTON_DESKTOP_INCLUDE_DIR libweston-desktop.h
  HINTS ${Libweston-Desktop_INCLUDE_DIRS})

find_library(LIBWESTON_LIB
  weston-6 weston-5 weston-4
  HINTS ${Libweston_LIBDIR})

find_library(LIBWESTON_DESKTOP_LIB
  weston-desktop-6 weston-desktop-5 weston-desktop-4
  HINTS ${Libweston-Desktop_LIBDIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBWESTON DEFAULT_MSG
  LIBWESTON_INCLUDE_DIR  LIBWESTON_DESKTOP_INCLUDE_DIR
  LIBWESTON_LIB LIBWESTON_DESKTOP_LIB)

set(LIBWESTON_INCLUDE_DIRS ${LIBWESTON_INCLUDE_DIR} ${LIBWESTON_DESKTOP_INCLUDE_DIR})

set(LIBWESTON_LIBRARIES ${LIBWESTON_LIB} ${LIBWESTON_DESKTOP_LIB})

mark_as_advanced(
  LIBWESTON_INCLUDE_DIR LIBWESTON_DESKTOP_INCLUDE_DIR
  LIBWESTON_LIB LIBWESTON_DESKTOP_LIB)

mark_as_advanced(
  Libweston_INCLUDE_DIRS  Libweston-Desktop_INCLUDR_DIRS
  )
