include(CTest)
enable_testing()

#test the features in this
include_directories(
  ${CLIENT_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  )

add_subdirectory(cursor)
include_directories(cursor)

# add_executable(test_weston-desktop
#   test-weston-desktop.c
#   )

# target_link_libraries(test_weston-desktop
#   ${COMPOSITOR_LIBRARIES}
#   ${TRDPTY_LIBRARIES}
#   )


add_executable(test_xkbcommon
  test-xkbcommon.c
  input.c
  input_data.c
  )

add_executable(test_hash
  testhash.c
  )
target_link_libraries(test_hash
  ${TRDPTY_LIBRARIES}
  )

target_link_libraries(test_xkbcommon
  ${CLIENT_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  twcursor
  m
  )

add_executable(test_cairo
  cairo_tutorial.c
  )

target_link_libraries(test_cairo
  ${CLIENT_LIBRARIES}
  )


add_executable(test_queue
  test_queue.c)

target_link_libraries(test_queue
  ${TRDPTY_LIBRARIES})


add_executable(test_pixman
  test-pixman.c
  )

target_link_libraries(test_pixman
  ${CLIENT_LIBARARIES})


add_executable(test_input
  test-input_nosurface.c
  )

target_link_libraries(test_input
  ${CLIENT_LIBRARIES}
  )


add_executable(test_fontconfig
  test_fontconfig.c)

target_link_libraries(test_fontconfig
  ${CLIENT_LIBRARIES}
  )


add_executable(test_nk_vulkan
  test_nk_vulkan.c)

target_link_libraries(test_nk_vulkan
  ${CLIENT_LIBRARIES}
  nk_vulkan_backend
  )


add_executable(test_nk_cairo
  test_nk_cairo.c)

target_link_libraries(test_nk_cairo
  nk_cairo_backend
  ${CLIENT_LIBRARIES}
  )


add_executable(test_user_font
  test_cairo_user_font.c)

target_link_libraries(test_user_font
  ${CLIENT_LIBRARIES}
  )
