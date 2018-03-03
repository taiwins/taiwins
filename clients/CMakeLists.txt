include_directories(
  ${CLIENT_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  )

#message("client library include: " ${CLIENT_INCLUDE_DIRS})
#message("client library: " ${CLIENT_LIBRARIES})

add_executable(shell-taiwins
  shell.c
  client.c
  buffer.c
  ui.c
  egl.c
  icon.c
  )

target_link_libraries(shell-taiwins
  ${CLIENT_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  ${WAYLAND_CURSOR_LIBRARIES}
  m
  proto_client_libraries
  )
