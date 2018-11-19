include_directories(
  ${CLIENT_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  )


add_library(nk_backend STATIC
  nk_wl_egl.c)

target_link_libraries(nk_backend
  ${CLIENT_LIBRARIES})


add_library(nk_cairo_backend STATIC
  nk_wl_cairo.c)


add_executable(test_nk_cairo
  dummy.c)

target_link_libraries(test_nk_cairo
  nk_cairo_backend
  ${CLIENT_LIBRARIES}
  taiwins_client
  )
