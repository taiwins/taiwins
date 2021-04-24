###### desktop shell
desktop_shell_src = [
  'desktop_shell/shell.c',
  'desktop_shell/shell_bg.c',
  'desktop_shell/shell_bus.c',
  'desktop_shell/shell_lock.c',
  'desktop_shell/shell_lua.c',
  'desktop_shell/shell_msgs.c',
  'desktop_shell/shell_panel.c',
  'desktop_shell/shell_ui.c',
  wayland_taiwins_shell_client_protocol_h,
  wayland_taiwins_shell_private_code_c,
  wayland_taiwins_theme_client_protocol_h,
  wayland_taiwins_theme_private_code_c,

  'widget/widget.c',
  'widget/battery.c',
  'widget/clock.c',
]
desktop_shell = executable(
  'taiwins-shell',
  desktop_shell_src,
  c_args : ['-D_GNU_SOURCE'],
  dependencies : [
    dep_wayland_client,
    dep_pam,
    dep_twclient,
    dep_twclient_theme,
    dep_nkcairo,
    dep_lua,
    dep_tdbus,
  ],
  include_directories : [
    inc_shared_config,
    include_directories('widget'),
  ],
  install : true,
)


###### desktop console
desktop_console_src = [
  'desktop_console/console.c',
  'desktop_console/console_module.c',
  'desktop_console/console_app.c',
  'desktop_console/console_cmd.c',
  'desktop_console/console_path.c',
  'desktop_console/console_config_lua.c',
  wayland_taiwins_shell_client_protocol_h,
  wayland_taiwins_shell_private_code_c,
  wayland_taiwins_theme_client_protocol_h,
  wayland_taiwins_theme_private_code_c,
  wayland_taiwins_console_client_protocol_h,
  wayland_taiwins_console_private_code_c,
]
desktop_console = executable(
  'taiwins-console',
  desktop_console_src,
  c_args : ['-D_GNU_SOURCE'],
  dependencies : [
    dep_wayland_client,
    dep_twclient,
    dep_twclient_theme,
    dep_nkcairo,
    dep_threads,
    dep_lua,
  ],
  include_directories : [
    inc_shared_config,
  ],
  install : true,
)


###### taiwins-update-cache
update_cache = executable(
  'taiwins-update-cache',
  'update_icon_cache.c',
  c_args : ['-D_GNU_SOURCE'],
  dependencies : [
    dep_wayland_client,
    dep_twclient,
    dep_twclient_icons,
  ],
  include_directories : [
    inc_shared_config,
  ],
  install : true,
)
