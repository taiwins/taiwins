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

add_library(proto_client_libraries STATIC
  ${proto_twshell_client}
  ${proto_twconsole_client}
  )

add_library(proto_server_libraries STATIC
  ${proto_twshell_server}
  ${proto_twconsole_server}
  ${proto_twtheme_server}
  )

set(PROTOCOLS_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR} PARENT_SCOPE)
message("protocols include dir: ${PROTOCOLS_INCLUDE_DIR}")
