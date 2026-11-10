add_library(i86testprog
  src/registry.cpp
  src/runner.cpp
  report/datalog.cpp
  report/summary.cpp)
add_library(i86::testprog ALIAS i86testprog)
target_include_directories(i86testprog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(i86testprog PUBLIC i86::tester i86::golden i86::spec)
i86_target_defaults(i86testprog)

# Object library so REGISTER_TEST static initializers are never dropped by the linker.
file(GLOB_RECURSE I86_CASE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cases/*.cpp)
add_library(i86cases OBJECT ${I86_CASE_SOURCES})
target_link_libraries(i86cases PUBLIC i86::testprog)
i86_target_defaults(i86cases)

add_executable(i86test cli/main.cpp)
target_link_libraries(i86test PRIVATE i86cases i86::testprog)
i86_target_defaults(i86test)

i86_add_unit_test(testprog_unit SOURCES tests/smoke_test.cpp LIBS i86cases i86::testprog)
