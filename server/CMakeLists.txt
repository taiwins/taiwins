include_directories(
  ${COMPOSITOR_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  )

add_library(twshell STATIC
  shell.c
  )

add_library(twconsole STATIC
  console.c)

target_link_libraries(twshell
  proto_server_libraries
  ${COMPOSITOR_LIBRARIES}
  )

add_library(taiwins_desktop STATIC
  desktop/desktop.c
  desktop/layout.c
  desktop/layout_floating.c
  desktop/layout_tiling.c
  desktop/workspace.c
  )

add_executable(taiwins
  main.c
  input.c
  backend.c
  taiwins.c
  )


target_link_libraries(taiwins
  ${COMPOSITOR_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  twshell
  twconsole
  taiwins_desktop
  )
