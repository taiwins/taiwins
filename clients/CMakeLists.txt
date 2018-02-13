include_directories(
  ${CLIENT_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  ${WAYLAND_CURSOR_INCLUDE_DIR}
  )

add_executable(shell-taiwins
  shell.c
  client.c
  )

target_link_libraries(shell-taiwins
  ${CLIENT_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  ${WAYLAND_CURSOR_LIBRARIES}
  m
  proto_client_libraries
  )
