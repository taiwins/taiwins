add_executable(shell-taiwins
  shell.c
  shell_bg.c
  shell_panel.c
  shell_lock.c
  shell_msgs.c

  widget.c
  widgets/clock.c
  widgets/battery.c
  widget_lua.c
  )

target_include_directories(shell-taiwins PUBLIC
  ${CLIENT_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  ${TRDPTY_INCLUDE_DIRS}
  ${LOVE_NK_SRC_DIR}
  )

target_link_libraries(shell-taiwins
  nk_cairo_backend
  ${CLIENT_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  ${WAYLAND_CURSOR_LIBRARIES}
  m
  proto_client_libraries
  )

add_executable(shell-console
  console.c
  )

target_include_directories(shell-console PUBLIC
  ${CLIENT_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  ${TRDPTY_INCLUDE_DIRS}
  )

target_link_libraries(shell-console
  nk_cairo_backend
  ${CLIENT_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  ${WAYLAND_CURSOR_LIBRARIES}
  m
  proto_client_libraries
  )

add_executable(taiwins_update_icon_cache
  update_icon_cache.c)


target_link_libraries(taiwins_update_icon_cache
  nk_cairo_backend
  )
