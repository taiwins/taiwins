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
  m
  )

###########################################
add_executable(test_config
  testconfig.c
  ../server/bus.c
  ../server/theme.c
  ../server/config/theme_lua.c
  ../server/config/config.c
  ../server/config/config_lua.c
  ../server/config/config_parser.c
  ../server/config/config_bindings.c
  )

target_include_directories(test_config PRIVATE
  ${COMPOSITOR_INCLUDE_DIRS}
  ${SERVER_DIR})


target_link_libraries(test_config
  ${COMPOSITOR_LIBRARIES}
  twcore
  tdbus
  twdesktop
  twclient::theme
  )


###########################################

add_executable(test_cairo_transform
  test_cairo_concept.c)

target_link_libraries(test_cairo_transform
  Cairo::Cairo
  ${CLIENT_LIBRARIES})

add_executable(test_raxinsert
  test_raxinsert.c
  )
target_link_libraries(test_raxinsert
  m
  rax
  )

add_executable(test_theme
  test_theme.c
  ../server/config/theme_lua.c
  )

target_link_libraries(
  test_theme
  libweston::libweston
  twclient::theme
  twclient::twclient
  twclient::nkcairo
  Lua::Lua
  ctypes
  m
  )
