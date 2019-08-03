include(Wayland)

WAYLAND_ADD_PROTOCOL_CLIENT(proto_twshell_client
  taiwins_desktop.xml
  taiwins-desktop
  )

WAYLAND_ADD_PROTOCOL_SERVER(proto_twshell_server
  taiwins_desktop.xml
  taiwins-desktop
  )


add_library(proto_client_libraries STATIC
  ${proto_twshell_client}
  )

add_library(proto_server_libraries STATIC
  ${proto_twshell_server}
  )

set(PROTOCOLS_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR} PARENT_SCOPE)
message("protocols include dir: ${PROTOCOLS_INCLUDE_DIR}")
