################# 3rdparties ####################

add_subdirectory("${CMAKE_SOURCE_DIR}/3rdparties/twclient")
add_subdirectory("${CMAKE_SOURCE_DIR}/3rdparties/iconheader")
add_subdirectory("${CMAKE_SOURCE_DIR}/3rdparties/nklua")

#################### rax ########################
set(RAX_DIR "${CMAKE_SOURCE_DIR}/3rdparties/rax")
find_path(RAX_INCLUDE_DIR rax.h
  HINTS ${RAX_DIR})

add_library(rax STATIC
  ${RAX_DIR}/rax.c
  )

#public means that the target uses rax also needs to include this
target_include_directories(rax PUBLIC ${RAX_INCLUDE_DIR})

target_link_libraries(rax PRIVATE
  m)
