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

add_library(twshell STATIC
  shell.c
  )

add_library(twconsole STATIC
  console.c)

add_library(twtheme STATIC
  theme.c)

add_library(twdesktop STATIC
  desktop/desktop.c
  desktop/layout.c
  desktop/layout_floating.c
  desktop/layout_tiling.c
  desktop/workspace.c
  )

target_link_libraries(twcore
  PUBLIC
  Wayland::Server
  libweston::libweston
  LibInput::LibInput
  Lua::Lua
  Pixman::Pixman
  XKBCommon::XKBCommon
  ctypes
  PRIVATE dl
  )

target_link_libraries(twshell PUBLIC
  twcore
  protos-server
  )

target_link_libraries(twconsole PUBLIC
  twcore
  protos-server
  )

target_link_libraries(twdesktop PUBLIC
  libweston::libweston-desktop
  twcore
  protos-server
  )

target_link_libraries(twtheme PUBLIC
  twcore
  protos-server
  )

################################################################################
# executable
################################################################################

add_executable(taiwins
  main.c
  )

target_link_libraries(taiwins
  twcore
  twshell
  twconsole
  twdesktop
  twtheme
  )

################################################################################
# install
################################################################################
include(GNUInstallDirs)
install(TARGETS taiwins DESTINATION ${CMAKE_INSTALL_BINDIR})
