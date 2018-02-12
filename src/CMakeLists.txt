include_directories(
  ${COMPOSITOR_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  )


add_executable(taiwins
  main.c
  input.c
  backend.c
  )

target_link_libraries(taiwins
  proto_server_libraries
  ${COMPOSITOR_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  )
