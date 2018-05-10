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
  add_definitions(-D__DEBUG)
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
find_package(Pixman REQUIRED)
find_package(PkgConfig)
find_package(XKBCommon REQUIRED)
find_package(LibInput REQUIRED)
pkg_search_module(Libweston libweston-4 libweston-3 libweston>=3 REQUIRED)
pkg_search_module(Libweston-Desktop libweston-desktop-4 libweston-desktop-3 libweston-desktop>=3 REQUIRED)
find_package(Wayland REQUIRED)
find_package(Cairo REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS EGL OpenGL)
find_package(Lua REQUIRED)
find_package(LibRSVG REQUIRED)
set(CMAKE_THREAD_PREFER_PTHREAD)
find_package(Threads REQUIRED)
#find_package(NVEGL)

#if(NVEGL_FOUND)
#  message("we are building with nvidia support")
#  add_definitions(-D_WITH_NVIDIA)
#endif(NVEGL_FOUND)



#####################################################################
################# setup the project variables #######################
add_subdirectory("3rdparties/ctypes")
add_subdirectory("3rdparties/nuklear")

set(COMPOSITOR_LIBRARIES
  ${Libweston_LIBRARIES}
  ${Libweston-Desktop_LIBRARIES}
  ${WAYLAND_SERVER_LIBRARIES}
  ${XKBCOMMON_LIBRARIES}
  ${PIXMAN_LIBRARIES}
  )
list(REMOVE_DUPLICATES COMPOSITOR_LIBRARIES)

set(COMPOSITOR_INCLUDE_DIRS
  ${Libweston_INCLUDE_DIRS}
  ${Libweston-Desktop_INCLUDE_DIRS}
  ${XKBCOMMON_INCLUDE_DIRS}
  ${WAYLAND_SERVER_INCLUDE_DIR}
  ${PIXMAN_INCLUDE_DIRS}
  )
list(REMOVE_DUPLICATES COMPOSITOR_INCLUDE_DIRS)

set(TRDPTY_INCLUDE_DIRS
  ${CTYPES_INCLUDE_DIR}
  ${NUKLEAR_INCLUDE_DIR}
  )
set(TRDPTY_LIBRARIES
  ${CTYPES_LIBRARIES}
  )

set(CLIENT_INCLUDE_DIRS
  ${XKBCOMMON_INCLUDE_DIRS}
  ${WAYLAND_CLIENT_INCLUDE_DIR}
  ${CAIRO_INCLUDE_DIRS}
  ${WAYLAND_CURSOR_INCLUDE_DIR}
  ${WAYLAND_EGL_INCLUDE_DIR}
  ${LUA_INCLUDE_DIR}
  ${LibRSVG_INCLUDE_DIRS}
  ${PIXMAN_INCLUDE_DIRS}
  )
list(REMOVE_DUPLICATES CLIENT_INCLUDE_DIRS)

set(CLIENT_LIBRARIES
  ${XKBCOMMON_LIBRARIES}
  ${WAYLAND_CLIENT_LIBRARIES}
  ${CAIRO_LIBRARIES}
  ${WAYLAND_EGL_LIBRARIES}
  # OpenGL::EGL
  # OpenGL::OpenGL
  ${OPENGL_LIBRARIES}
  ${OPENGL_egl_LIBRARY}
  ${LUA_LIBRARIES}
  ${LibRSVG_LIBRARIES}
  Threads::Threads
  ${PIXMAN_LIBRARIES}
  )
list(REMOVE_DUPLICATES CLIENT_LIBRARIES)

include_directories(${COMPOSITOR_INCLUDE_DIRS})
include_directories(${TRDPTY_INCLUDE_DIRS})


#####################################################################
message("3rdparty includes: ${TRDPTY_INCLUDE_DIRS}")
message("3rdparty lib: ${TRDPTY_LIBRARIES}")

add_subdirectory(protocols)
add_subdirectory(src)
add_subdirectory(test)
add_subdirectory(clients)
