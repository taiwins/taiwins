include_directories(
  ${CLIENT_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  ${TRDPTY_INCLUDE_DIRS}
  )

add_executable(shell-taiwins
  shell.c
  widget.c
  widgets/clock.c
  widgets/battery.c
  )

target_link_libraries(shell-taiwins
  ${CLIENT_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  ${WAYLAND_CURSOR_LIBRARIES}
  m
  proto_client_libraries
  )

add_executable(shell-console
  console.c
  )

target_link_libraries(shell-console
  nk_egl_backend
  ${CLIENT_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  ${WAYLAND_CURSOR_LIBRARIES}
  m
  proto_client_libraries
  )
