cmake_minimum_required(VERSION 3.1.0)

project(taiwins
  VERSION 0.1
  LANGUAGES C)

set(CMAKE_BINARY_DIR build)
set(CMAKE_C_FLAGS "-g")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions(
  -D_GNU_SOURCE
  )
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall")


set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${PROJECT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)

find_package(PkgConfig)
#find_package(JsonC REQUIRED)
#find_package(PCRE REQUIRED)
#find_package(WLC REQUIRED)
#find_package(Wayland REQUIRED)
find_package(XKBCommon REQUIRED)
#find_package(Cairo)
#find_package(Pango)
#find_package(GdkPixbuf)
#find_package(PAM)
find_package(LibInput REQUIRED)
pkg_check_modules(Waylandserver wayland-server REQUIRED)
pkg_check_modules(Libweston libweston-3 REQUIRED)
pkg_check_modules(Libweston-Desktop libweston-desktop-3 REQUIRED)

set(COMPOSITOR_LIBRARIES
  ${Libweston_LIBRARIES}
  ${Libweston-Desktop_LIBRARIES}
  ${Waylandserver_LIBRARIES}
  )

set(COMPOSITOR_INCLUDE_DIRS
  ${Libweston_INCLUDE_DIRS}
  ${Libweston-Desktop_INCLUDE_DIRS}
  ${Waylandserver_INCLUDE_DIRS})

include_directories(${COMPOSITOR_INCLUDE_DIRS})


#runtime output dir, but it ruined everything
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

#this program test many features that I need to test, if the function passed. I will use in the future
add_executable(test_weston-desktop
  test/test-weston-desktop.c
  )

target_link_libraries(test_weston-desktop
  ${COMPOSITOR_LIBRARIES}
  )

#include_directories(include)
#add_subdirectory(protocols)
add_subdirectory(src)
#add_subdirectory(clients)
