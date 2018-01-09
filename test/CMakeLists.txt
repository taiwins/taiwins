#test the features in this
include_directories(${CLIENT_INCLUDE_DIRS})
add_executable(test_weston-desktop
  test-weston-desktop.c
  )

target_link_libraries(test_weston-desktop
  ${COMPOSITOR_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  )


add_executable(test_xkbcommon
  test-xkbcommon.c
  input.c
  input_data.c
  )

target_link_libraries(test_xkbcommon
  ${CLIENT_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  )
