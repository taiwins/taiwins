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

add_executable(taiwins
  main.c
  input.c
  backend.c
  shell.c
  )



target_link_libraries(taiwins
  ${COMPOSITOR_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  twshell
  )
