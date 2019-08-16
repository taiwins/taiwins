include(CTest)
enable_testing()

#test the features in this
include_directories(
  ${CLIENT_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  )

add_subdirectory(cursor)
include_directories(cursor)


add_executable(test_hash
  testhash.c
  )
target_link_libraries(test_hash
  ${TRDPTY_LIBRARIES}
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
  ${PIXMAN_LIBRARIES}
  ${CLIENT_LIBARARIES})


# add_executable(test_input
#   test-input_nosurface.c
#   input.1.c
#   )

# target_link_libraries(test_input
#   ${CLIENT_LIBRARIES}
#   ${TRDPTY_LIBRARIES}
#   )


add_executable(test_fontconfig
  test_fontconfig.c)

target_link_libraries(test_fontconfig
  ${CLIENT_LIBRARIES}
  )



add_executable(test_user_font
  test_cairo_user_font.c)

target_link_libraries(test_user_font
  ${CLIENT_LIBRARIES}
  )

###########################################
add_executable(test_lua
  luatest.c
  )

target_include_directories(test_lua PRIVATE
  ${COMPOSITOR_INCLUDE_DIRS}
  ${SERVER_DIR})


target_link_libraries(test_lua
  ${COMPOSITOR_LIBRARIES}
  twcore
  )

###########################################
add_executable(test_config
  testconfig.c
  )

target_include_directories(test_config PRIVATE
  ${COMPOSITOR_INCLUDE_DIRS}
  ${SERVER_DIR})


target_link_libraries(test_config
  ${COMPOSITOR_LIBRARIES}
  twcore
  )


###########################################
add_executable(test_register
  test_binding_register.c)


target_link_libraries(test_register
  ${CLIENT_LIBRARIES}
  ${TRDRTY_LIBRARIES})


add_executable(test_cairo_transform
  test_cairo_concept.c)

target_link_libraries(test_cairo_transform
  ${CLIENT_LIBRARIES})
