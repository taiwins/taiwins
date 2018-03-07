find_package(PkgConfig)
pkg_check_modules(pc_nv_egl QUIET wayland-eglstream)

set(NVEGL_INCLUDE_DIRS ${pc_nv_egl_INCLUDE_DIRS})
set(NVEGL_LIBRARIES ${pc_nv_egl_LIBRARIES})

find_package_handle_standard_args(NVEGL DEFAULT_MSG NVEGL_INCLUDE_DIRS NVEGL_LIBRARIES)
