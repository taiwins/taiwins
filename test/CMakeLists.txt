include(CTest)
enable_testing()

add_subdirectory(cursor)
include_directories(cursor)

add_executable(test_pixman
  test-pixman.c
  )
target_link_libraries(test_pixman
  Pixman::Pixman
  )

add_executable(test_fontconfig
  test_fontconfig.c
  )
target_link_libraries(test_fontconfig
  Cairo::Cairo
  Fontconfig::Fontconfig
  )

add_executable(test_user_font
  test_cairo_user_font.c
  )
target_link_libraries(test_user_font
  Cairo::Cairo
  Freetype::Freetype
  )

add_executable(test_lua
  luatest.c
  )
target_link_libraries(test_lua
  Lua::Lua
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

add_executable(test_cairo_transform
  test_cairo_concept.c)

target_link_libraries(test_cairo_transform
  Cairo::Cairo
  ${CLIENT_LIBRARIES})


add_executable(test_widget_lua
  test_widget_lua.c
  ../clients/widget.c
  ../clients/widget_lua.c
  ../clients/widgets/clock.c
  ../clients/widgets/battery.c
  )

# target_include_directories(test_widget_lua PRIVATE
#   "${CMAKE_SOURCE_DIR}/clients"
#   ${CLIENT_INCLUDE_DIRS}
#   ${PROTOCOLS_INCLUDE_DIR}
#   ${TRDPTY_INCLUDE_DIRS}
#   ${LOVE_NK_SRC_DIR}
#   )

target_link_libraries(test_widget_lua
  twclient::twclient
  twclient::nkcairo
  protos-client
  nuklear::love
  ctypes
  m
  )

add_executable(test_raxinsert
  test_raxinsert.c
  )
target_link_libraries(test_raxinsert
  m
  rax
  )
