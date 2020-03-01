################# 3rdparties ####################

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/twclient")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/nklua")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/tdbus")

################## protocols #####################
set(TAIWINS_PROTOCOLS_DIR
  "${CMAKE_CURRENT_LIST_DIR}/taiwins-protocols"
  PARENT_SCOPE)

#################### rax ########################
set(RAX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rax")
find_path(RAX_INCLUDE_DIR rax.h
  HINTS ${RAX_DIR})

add_library(rax STATIC
  ${RAX_DIR}/rax.c
  )

#public means that the target uses rax also needs to include this
target_include_directories(rax PUBLIC ${RAX_INCLUDE_DIR})

target_link_libraries(rax PRIVATE
  m)

#################### libweston ########################
include(ExternalProject)

find_program(NINJA_PROGRAM ninja REQUIRED)
find_program(MESON_PROGRAM meson REQUIRED)


set(LIBWESTON_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/libweston")
set(LIBWESTON_BUILD_DIR "${CMAKE_BINARY_DIR}/libweston")
set(LIBWESTON_INSTALL_DIR "${CMAKE_BINARY_DIR}/libweston-install")
## NOTE: meson will usually set the libdir to multiarch on debian systems. But
## cmake only does it when prefix is /usr. We set it directly into lib to
## avoid that problem.
set(LIBWESTON_OPTIONS "--prefix=${LIBWESTON_INSTALL_DIR} --libdir=lib -Dsimple-dmabuf-drm= -Dbackend-rdp=false")
set(LIBWESTON_INSTALL_LIBDIR "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libweston-taiwins")

include(WestonVersionParser)
ParseWestonVersion(VER ${LIBWESTON_SRC_DIR})

ExternalProject_Add(pj_libweston
  SOURCE_DIR "${LIBWESTON_SRC_DIR}"
  BINARY_DIR "${LIBWESTON_BUILD_DIR}"
  INSTALL_DIR "${CMAKE_INSTALL_PREFIX}"
  #config
  CONFIGURE_COMMAND sh -c "meson ${LIBWESTON_OPTIONS} ${LIBWESTON_BUILD_DIR} ${LIBWESTON_SRC_DIR}"
  #build
  BUILD_COMMAND sh -c "ninja -C ${LIBWESTON_BUILD_DIR} install"
  BUILD_ALWAYS ON
  #install, it seems always run
  INSTALL_COMMAND ""
  )

#add step to build our own libweston lib
ExternalProject_Add_Step(pj_libweston rename
  COMMENT "install libweston to custom location"
  DEPENDEES build
  ALWAYS on
  COMMAND sh -c "install -m755 -d ${LIBWESTON_INSTALL_LIBDIR}"
  COMMAND sh -c "cp -av ${LIBWESTON_INSTALL_DIR}/lib/libweston-${VER}.* ${LIBWESTON_INSTALL_LIBDIR}"
  COMMAND sh -c "cp -av ${LIBWESTON_INSTALL_DIR}/lib/libweston-desktop-${VER}.* ${LIBWESTON_INSTALL_LIBDIR}"
  COMMAND sh -c "cp -av ${LIBWESTON_INSTALL_DIR}/lib/libweston-${VER}/* ${LIBWESTON_INSTALL_LIBDIR}/"
  )

#getting the properties
add_library(libweston::libweston SHARED IMPORTED GLOBAL)
add_dependencies(libweston::libweston pj_libweston)
set_target_properties(libweston::libweston PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES "C"
  INTERFACE_INCLUDE_DIRECTORIES "${LIBWESTON_SRC_DIR}/include"
  IMPORTED_LOCATION "${LIBWESTON_INSTALL_LIBDIR}/libweston-${VER}.so"
  )

add_library(libweston::libweston-desktop SHARED IMPORTED GLOBAL)
add_dependencies(libweston::libweston-desktop pj_libweston)
set_target_properties(libweston::libweston-desktop PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES "C"
  INTERFACE_INCLUDE_DIRECTORIES "${LIBWESTON_SRC_DIR}/include"
  IMPORTED_LOCATION "${LIBWESTON_INSTALL_LIBDIR}/libweston-desktop-${VER}.so"
  )

################################################################################
# install
################################################################################
include(GNUInstallDirs)
set(CMAKE_INSTALL_RPATH
  ${CMAKE_INSTALL_RPATH}
  ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libweston-taiwins
  PARENT_SCOPE)
install(DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libweston-taiwins
  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  USE_SOURCE_PERMISSIONS)
