add_library(i86common INTERFACE)
add_library(i86::common ALIAS i86common)
target_include_directories(i86common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
