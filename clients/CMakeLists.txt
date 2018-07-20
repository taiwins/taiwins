include_directories(
  ${CLIENT_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  )

#message("client library include: " ${CLIENT_INCLUDE_DIRS})
#message("client library: " ${CLIENT_LIBRARIES})
add_library(taiwins_client
  client.c
  appsurf.c
  ui.c
  egl.c
  buffer.c
  )

add_library(nk_backend STATIC
  nk_wl_egl.c)

target_link_libraries(nk_backend
  ${CLIENT_LIBRARIES})

target_link_libraries(taiwins_client
  ${CLIENT_LIBRARIES}
  ${WAYLAND_CURSOR_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  )

add_executable(shell-taiwins
  shell.c
#  shellui_gl.c
  icon.c
  shellui.c
  )
#message("the NVEGL libraries are ${NVEGL_LIBRARIES}")
# set_target_properties(shell-taiwins
#   PROPERTIES COMPILE_FLAGS
#   "-fsanitize=address -fno-omit-frame-pointer")


target_link_libraries(shell-taiwins
  ${CLIENT_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  ${WAYLAND_CURSOR_LIBRARIES}
  ${NVEGL_LIBRARIES}
  m
  proto_client_libraries
  taiwins_client
  nk_backend
  tcmalloc
#  asan
  )



add_executable(shell-launcher
  launcher.c
  )

target_link_libraries(shell-launcher
  ${CLIENT_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  ${WAYLAND_CURSOR_LIBRARIES}
  m
  proto_client_libraries
  nk_backend
  taiwins_client
  )


add_executable(dummy
  dummy.c
  )

set_target_properties(dummy
  PROPERTIES COMPILE_FLAGS
  "-fsanitize=address -fno-omit-frame-pointer")

target_link_libraries(dummy
  ${CLIENT_LIBRARIES}
  taiwins_client
  nk_backend
  asan
  )
