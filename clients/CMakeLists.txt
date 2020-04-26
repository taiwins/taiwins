add_executable(taiwins-shell
  desktop_shell/shell.c
  desktop_shell/shell_bg.c
  desktop_shell/shell_panel.c
  desktop_shell/shell_lock.c
  desktop_shell/shell_msgs.c
  desktop_shell/shell_bus.c
  desktop_shell/shell_ui.c

  widget/widget.c
  widget/clock.c
  widget/battery.c
  widget/widget_lua.c
  )
target_include_directories(taiwins-shell
  PRIVATE ${SHARED_CONFIG_DIR} ${CMAKE_CURRENT_LIST_DIR}
  )
target_link_libraries(taiwins-shell
  protos-client
  tdbus
  twclient::nkcairo
  Wayland::Cursor
  nuklear::love
  PAM::PAM
  m
  )


add_executable(taiwins-console
  desktop_console/console.c
  desktop_console/console_module.c
  desktop_console/console_cmd.c
  desktop_console/console_app.c
  )
target_include_directories(taiwins-console
  PRIVATE ${SHARED_CONFIG_DIR} ${CMAKE_CURRENT_LIST_DIR}
  )
target_link_libraries(taiwins-console
  protos-client
  twclient::nkcairo
  twclient::icons
  Wayland::Cursor
  Threads::Threads
  Lua::Lua
  rax
  m
  )

add_executable(taiwins-update-icon-cache
  update_icon_cache.c
  )
target_include_directories(taiwins-update-icon-cache
  PRIVATE ${SHARED_CONFIG_DIR} ${CMAKE_CURRENT_LIST_DIR}
  )
target_link_libraries(taiwins-update-icon-cache
  twclient::icons
  twclient::nkcairo
  )

################################################################################
# install
################################################################################
include(GNUInstallDirs)
install(TARGETS taiwins-shell taiwins-console taiwins-update-icon-cache
  DESTINATION ${CMAKE_INSTALL_BINDIR})
