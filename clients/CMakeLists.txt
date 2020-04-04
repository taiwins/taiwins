add_executable(taiwins-shell
  shell.c
  shell_bg.c
  shell_panel.c
  shell_lock.c
  shell_msgs.c
  shell_bus.c
  shell_ui.c

  widget.c
  widgets/clock.c
  widgets/battery.c
  widget_lua.c
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
  console.c
  console_module/console_module.c
  console_module/console_cmd.c
  console_module/console_app.c
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
