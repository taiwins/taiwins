include_directories(
  ${COMPOSITOR_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  )

add_library(twshell
  shell.c
  )

target_link_libraries(twshell
  proto_server_libraries
  ${COMPOSITOR_LIBRARIES}
  )

add_library(taiwins_desktop
  desktop.c
  layout/layout.c)

add_executable(taiwins
  main.c
  input.c
  backend.c
  taiwins.c
  launcher.c
  )


target_link_libraries(taiwins
  ${COMPOSITOR_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  twshell
  taiwins_desktop
  )
