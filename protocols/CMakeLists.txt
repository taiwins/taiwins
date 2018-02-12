include(Wayland)

WAYLAND_ADD_PROTOCOL_CLIENT(proto_twshell_client
  taiwins_shell.xml
  taiwins-shell
  )

WAYLAND_ADD_PROTOCOL_SERVER(proto_twshell_server
  taiwins_shell.xml
  taiwins-shell
  )


add_library(proto_client_libraries STATIC
  ${proto_twshell_client}
  )

add_library(proto_server_libraries STATIC
  ${proto_twshell_server}
  )

set(PROTOCOLS_INCLUDE_DIR ${CMAKE_BINARY_DIR}/protocols CACHE PATH "protocols include directory")
message("protocols include dir: ${PROTOCOLS_INCLUDE_DIR}")
