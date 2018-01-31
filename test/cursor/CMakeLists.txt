include_directories(${CLIENT_INCLUDE_DIRS})

add_library(wayland-cursor
  wayland-cursor.c
  xcursor.c
  os-compatibility.c
  )

target_link_libraries(wayland-cursor
  ${CLIENT_LIBRARIES}
  )
