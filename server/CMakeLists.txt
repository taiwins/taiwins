################################################################################
# library
################################################################################

add_library(twcore STATIC
  backend.c
  taiwins.c
  bindings.c
  xwayland.c # need to be an option
  )
target_include_directories(twcore
  PUBLIC ${SHARED_CONFIG_DIR}
  )
target_link_libraries(twcore
  PUBLIC
  protos-server
  Wayland::Server
  libweston::libweston
  LibInput::LibInput
  Lua::Lua
  Pixman::Pixman
  XKBCommon::XKBCommon
  ctypes
  PRIVATE dl
  )


add_library(twdesktop STATIC
  desktop/desktop.c
  desktop/layout.c
  desktop/layout_floating.c
  desktop/layout_tiling.c
  desktop/workspace.c

  desktop/shell.c
  desktop/console.c
  )

target_link_libraries(twdesktop PUBLIC
  libweston::libweston-desktop
  twcore
  protos-server
  )

################################################################################
# executable
################################################################################

add_executable(taiwins
  compositor.c
  theme.c
  bus.c

  config/config_parser.c
  config/config.c
  config/config_lua.c
  config/config_bindings.c
  config/theme_lua.c
  )
target_link_libraries(taiwins
  twcore
  twdesktop
  tdbus
  twclient::theme
  )

################################################################################
# install
################################################################################
include(GNUInstallDirs)
install(TARGETS taiwins DESTINATION ${CMAKE_INSTALL_BINDIR})
