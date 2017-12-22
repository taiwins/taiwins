include_directories(${COMPOSITOR_INCLUDE_DIRS})


add_executable(taiwins
  main.c
  input.c
  )

target_link_libraries(taiwins
  ${COMPOSITOR_LIBRARIES}
  )
