find_path(FONTCONFIG_INCLUDE_DIR fontconfig/fontconfig.h)

find_library(FONTCONFIG_LIBRARY NAMES fontconfig)

# handle the QUIETLY and REQUIRED arguments and set FONTCONFIG_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FontConfig DEFAULT_MSG
  FONTCONFIG_LIBRARY  FONTCONFIG_INCLUDE_DIR)

if(FONTCONFIG_FOUND)
  set( FONTCONFIG_LIBRARIES ${FONTCONFIG_LIBRARY} )
endif()

mark_as_advanced(FONTCONFIG_INCLUDE_DIR FONTCONFIG_LIBRARY FONTCONFIG_LIBRARIES)
