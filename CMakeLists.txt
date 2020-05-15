cmake_minimum_required(VERSION 3.12.0)

project(taiwins
  VERSION 0.1.1
  LANGUAGES C)

################################################################################
# options
################################################################################
option(INSTALL_TWCLIENT "install taiwins client library" OFF)

################################################################################
# project setup
################################################################################
option(BUILD_DOC "Build documentation" OFF)

add_definitions(-D_GNU_SOURCE)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
if(CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions(-D__DEBUG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wextra -ggdb3 -fsanitize=address")
endif()

if(CMAKE_BUILD_TYPE MATCHES Release)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -O2")
endif()

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${PROJECT_SOURCE_DIR}/cmake")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

################################################################################
# find packages
################################################################################
find_package(Pixman REQUIRED)
find_package(PkgConfig)
find_package(XKBCommon REQUIRED)
find_package(LibInput REQUIRED)
find_package(Wayland REQUIRED)
find_package(Cairo REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS EGL OpenGL)
find_package(Lua REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(Freetype REQUIRED)
find_package(PAM REQUIRED)
find_package(Doxygen COMPONENTS dot
  OPTIONAL_COMPONENTS mscgen dia)

set(CMAKE_THREAD_PREFER_PTHREAD)
find_package(Threads REQUIRED)


################################################################################
# 3rdparties
################################################################################
add_subdirectory(subprojects)

################################################################################
# message
################################################################################
message("build options: ${CMAKE_C_FLAGS}")
message("3rdparty includes: ${TRDPTY_INCLUDE_DIRS}")
message("3rdparty lib: ${TRDPTY_LIBRARIES}")

set(SERVER_DIR "${CMAKE_SOURCE_DIR}/server")
set(CLINET_DIR "${CMAKE_SOURCE_DIR}/clients")
set(SHARED_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(protocols)
add_subdirectory(server)
add_subdirectory(test)
add_subdirectory(clients)

################################################################################
# uninstall
################################################################################
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)

add_custom_target(uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")

################################################################################
# doc
################################################################################
if (DOXYGEN_FOUND AND BUILD_DOC)
  message("Doxygen started")
  set(DOXYGEN_CALL_GRAPH YES)
  set(DOXYGEN_CALLER_GRAPH YES)
  set(DOXYGEN_GENERATE_XML YES)
  set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/docs)
  set(DOXYGEN_EXCLUDE_PATTERNS
    "*/cmake/*"
    "*/nuklear/*"
    "*/stb/*"
    "*/test/*"
    "*/twclient/theme.h")

  doxygen_add_docs(doxygen
    ${PROJECT_SOURCE_DIR}/docs
    ${PROJECT_SOURCE_DIR}/server
    ${PROJECT_SOURCE_DIR}/clients
    ${PROJECT_SOURCE_DIR}/subprojects/twclient
    )

  add_custom_target(doc_doxygen ALL
    COMMAND Doxygen::doxygen Doxyfile.doxygen
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen"
    )

endif()
