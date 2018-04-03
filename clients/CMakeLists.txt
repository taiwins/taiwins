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

target_link_libraries(taiwins_client
  ${CLIENT_LIBRARIES}
  ${WAYLAND_CURSOR_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  )

add_executable(shell-taiwins
  shell.c
  shellui_gl.c
  icon.c
  shellui.c
  )
#message("the NVEGL libraries are ${NVEGL_LIBRARIES}")

target_link_libraries(shell-taiwins
  ${CLIENT_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  ${WAYLAND_CURSOR_LIBRARIES}
  ${NVEGL_LIBRARIES}
  m
  proto_client_libraries
  taiwins_client
  )
