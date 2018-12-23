include_directories(
  ${COMPOSITOR_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  )

add_library(twshell STATIC
  shell.c
  )

add_library(twcommander STATIC
  commander.c)

target_link_libraries(twshell
  proto_server_libraries
  ${COMPOSITOR_LIBRARIES}
  )

add_library(taiwins_desktop STATIC
  desktop/desktop.c
  desktop/layout.c
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
  twcommander
  taiwins_desktop
  )
