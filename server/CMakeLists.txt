include_directories(
  ${COMPOSITOR_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  )

add_library(twcore STATIC
  backend.c
  taiwins.c
  config.c
  bindings.c
  )

add_library(twshell STATIC
  shell.c
  )

add_library(twconsole STATIC
  console.c)

add_library(twdesktop STATIC
  desktop/desktop.c
  desktop/layout.c
  desktop/layout_floating.c
  desktop/layout_tiling.c
  desktop/workspace.c
  )

target_link_libraries(twshell PRIVATE
  twcore)

target_link_libraries(twconsole PRIVATE
  twcore
  )

target_link_libraries(twdesktop PRIVATE
  twcore
  )

target_include_directories(twdesktop PRIVATE
  ${COMPOSITOR_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  )

############################################
## taiwins executable
############################################

add_executable(taiwins
  main.c
  )

target_link_libraries(taiwins
  twcore
  twshell
  twconsole
  twdesktop
  proto_server_libraries
  ${COMPOSITOR_LIBRARIES}
  ${TRDPTY_LIBRARIES}
  )
