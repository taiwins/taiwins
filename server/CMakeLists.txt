################################################################################
# library
################################################################################

add_library(twcore STATIC
  backend.c
  taiwins.c
  bindings.c

  config/config_parser.c
  config/config.c
  config/config_lua.c
  )

add_library(twbus STATIC
  bus.c
  )

add_library(twtheme STATIC
  theme.c
  config/theme_lua.c
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

target_link_libraries(twbus
  PUBLIC
  tdbus
  twcore
  )

target_link_libraries(twdesktop PUBLIC
  libweston::libweston-desktop
  twcore
  protos-server
  )

target_link_libraries(twtheme PUBLIC
  twcore
  protos-server
  twclient::theme
  )

################################################################################
# executable
################################################################################

add_executable(taiwins
  main.c
  )

target_link_libraries(taiwins
  twcore
  twdesktop
  twtheme
  twbus
  )

################################################################################
# install
################################################################################
include(GNUInstallDirs)
install(TARGETS taiwins DESTINATION ${CMAKE_INSTALL_BINDIR})
