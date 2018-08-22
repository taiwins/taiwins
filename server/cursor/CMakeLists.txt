include_directories(${CLIENT_INCLUDE_DIRS})

add_library(twcursor
  cursor.c
  xcursor.c
  os-compatibility.c
  )

target_link_libraries(twcursor
  ${CLIENT_LIBRARIES}
  )
