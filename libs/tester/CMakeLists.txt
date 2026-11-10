add_library(i86tester
  src/sim/sim_tester.cpp
  src/sim/engine.cpp
  src/sim/timing.cpp
  src/electrical/electrical_model.cpp
  src/pattern.cpp
  src/timing_expr.cpp
  src/memory_image.cpp)
add_library(i86::tester ALIAS i86tester)
target_include_directories(i86tester PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(i86tester PUBLIC i86::model i86::spec)
i86_target_defaults(i86tester)

i86_add_unit_test(tester_unit
  SOURCES tests/tester_test.cpp tests/pattern_test.cpp tests/electrical_test.cpp
          tests/sim_tester_test.cpp tests/fake_device.cpp
  LIBS i86::tester)
if(TARGET tester_unit)
  target_include_directories(tester_unit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()
