cmake_minimum_required(VERSION 3.1.0)

project(taiwins
  VERSION 0.1
  LANGUAGES C)


##############setup all the variables, standdards####################
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()
if(CMAKE_BUILD_TYPE MATCHES Debug)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions(
  -D_GNU_SOURCE
  )

#####################################################################


############## setup the directories ################################
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${PROJECT_SOURCE_DIR}/cmake")
set(CMAKE_BINARY_DIR "${CMAKE_SOURCE_DIR}/build")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
#####################################################################


###################### find packages ################################
include(GNUInstallDirs)
find_package(PkgConfig)
#the wayland protocols is now standard, you need find the wayland-protocols, and
#include all the wayland protocols
find_package(XKBCommon REQUIRED)
find_package(LibInput REQUIRED)
pkg_check_modules(Libweston libweston-3 REQUIRED)
pkg_check_modules(Libweston-Desktop libweston-desktop-3 REQUIRED)
find_package(Wayland REQUIRED)
find_package(Cairo REQUIRED)


#####################################################################
################# setup the project variables #######################
add_subdirectory("3rdparties/ctypes")

set(COMPOSITOR_LIBRARIES
  ${Libweston_LIBRARIES}
  ${Libweston-Desktop_LIBRARIES}
  ${WAYLAND_SERVER_LIBRARIES}
  ${XKBCOMMON_LIBRARIES}
  )
set(COMPOSITOR_INCLUDE_DIRS
  ${Libweston_INCLUDE_DIRS}
  ${Libweston-Desktop_INCLUDE_DIRS}
  ${XKBCOMMON_INCLUDE_DIRS}
  ${WAYLAND_SERVER_INCLUDE_DIR}
  )

set(TRDPTY_INCLUDE_DIRS
  ${CTYPES_INCLUDE_DIR}
  )
set(TRDPTY_LIBRARIES
  ${CTYPES_LIBRARIES}
  )

set(CLIENT_INCLUDE_DIRS
  ${XKBCOMMON_INCLUDE_DIRS}
  ${WAYLAND_CLIENT_INCLUDE_DIR}
  ${CAIRO_INCLUDE_DIRS}
#  ${WAYLAND_CURSOR_INCLUDE_DIR}
  )
set(CLIENT_LIBRARIES
  ${XKBCOMMON_LIBRARIES}
  ${WAYLAND_CLIENT_LIBRARIES}
  ${CAIRO_LIBRARIES}
#  ${WAYLAND_CURSOR_LIBRARIES}
  )


include_directories(${COMPOSITOR_INCLUDE_DIRS})
include_directories(${TRDPTY_INCLUDE_DIRS})


#####################################################################
message("3rdparty includes: ${TRDPTY_INCLUDE_DIRS}")
message("3rdparty lib: ${TRDPTY_LIBRARIES}")

add_subdirectory(src)
add_subdirectory(test)
