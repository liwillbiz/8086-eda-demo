add_library(i86spec src/spec.cpp)
add_library(i86::spec ALIAS i86spec)
target_include_directories(i86spec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(i86spec PUBLIC i86::common PRIVATE yaml-cpp::yaml-cpp)
i86_target_defaults(i86spec)

i86_add_unit_test(spec_unit SOURCES tests/spec_test.cpp LIBS i86::spec)
