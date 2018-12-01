include_directories(
  ${CLIENT_INCLUDE_DIRS}
  ${PROTOCOLS_INCLUDE_DIR}
  Vulkan::Vulkan
  )

add_library(nk_egl_backend STATIC
  nk_wl_egl.c)

target_link_libraries(nk_egl_backend
  ${CLIENT_LIBRARIES})

add_library(nk_cairo_backend STATIC
  nk_wl_cairo.c)

target_link_libraries(nk_cairo_backend
  ${CLIENT_LIBRARIES}
  taiwins_client)

add_library(nk_vulkan_backend STATIC
  nk_wl_vulkan.c
  )

target_link_libraries(nk_vulkan_backend
  Vulkan::Vulkan
  taiwins_client
  )
