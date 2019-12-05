################################################################################
# library
################################################################################

include(Wayland)

WAYLAND_ADD_PROTOCOL_CLIENT(proto_twshell_client
  taiwins_shell.xml
  taiwins-shell
  )

WAYLAND_ADD_PROTOCOL_SERVER(proto_twshell_server
  taiwins_shell.xml
  taiwins-shell
  )

WAYLAND_ADD_PROTOCOL_CLIENT(proto_twconsole_client
  taiwins_console.xml
  taiwins-console
  )

WAYLAND_ADD_PROTOCOL_SERVER(proto_twconsole_server
  taiwins_console.xml
  taiwins-console
  )

WAYLAND_ADD_PROTOCOL_SERVER(proto_twtheme_server
  taiwins_theme.xml
  taiwins-theme
  )

add_library(protos-client STATIC
  ${proto_twshell_client}
  ${proto_twconsole_client}
  )

add_library(protos-server STATIC
  ${proto_twshell_server}
  ${proto_twconsole_server}
  ${proto_twtheme_server}
  )

target_include_directories(protos-client PUBLIC
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
target_include_directories(protos-server PUBLIC
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

################################################################################
# install
################################################################################
include(GNUInstallDirs)
install(FILES taiwins_shell.xml taiwins_console.xml
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/taiwins/protocols)
