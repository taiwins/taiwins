find_package(PkgConfig)
pkg_search_module(LUA REQUIRED QUIET lua lua53)
find_library(LUA_LOCATION NAMES lua lua53 lua5.3 HINTS ${LUA_LIBRARY_DIRS} ${LUA_LIBDIR})
#deal with case where missing
if(NOT LUA_INCLUDE_DIRS)
  set(LUA_INCLUDE_DIRS ${LUA_INCLUDEDIR})
endif()

if(NOT LUA_LIBRARY_DIRS)
  set(LUA_LIBRARY_DIRS ${LUA_LIBDIR})
endif()

# Hide advanced variables from CMake GUIs
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Lua DEFAULT_MSG LUA_LIBRARIES LUA_INCLUDE_DIRS)
MARK_AS_ADVANCED(LUA_LIBRARIES LUA_INCLUDE_DIRS)

if (CAIRO_FOUND AND NOT TARGET Lua::Lua)
  add_library(Lua::Lua UNKNOWN IMPORTED)
  set_target_properties(Lua::Lua PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    INTERFACE_LINK_DIRECTORIES "${LUA_LIBRARY_DIRS}"
    INTERFACE_LINK_LIBRARIES "${LUA_LIBRARIES}"
    IMPORTED_LOCATION "${LUA_LOCATION}"
    INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIRS}"
    )
endif()
