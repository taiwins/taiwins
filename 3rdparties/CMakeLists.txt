################# 3rdparties ####################

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/twclient")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/nklua")

#################### rax ########################
set(RAX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rax")
find_path(RAX_INCLUDE_DIR rax.h
  HINTS ${RAX_DIR})

add_library(rax STATIC
  ${RAX_DIR}/rax.c
  )

#public means that the target uses rax also needs to include this
target_include_directories(rax PUBLIC ${RAX_INCLUDE_DIR})

target_link_libraries(rax PRIVATE
  m)

#################### rax ########################
