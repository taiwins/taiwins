include_directories(${COMPOSITOR_INCLUDE_DIRS})


add_executable(taiwins
  main.c
  input.c
  backend.c
  )

target_link_libraries(taiwins
  ${COMPOSITOR_LIBRARIES}
  ${THRDPTY_LIBRARIES}
  )
