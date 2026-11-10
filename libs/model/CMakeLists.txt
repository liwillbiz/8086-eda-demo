add_library(i86model
  src/behavioral/behavioral_model.cpp
  src/behavioral/core_biu.cpp
  src/behavioral/core_eu.cpp
  src/fault/fault_injector.cpp
  src/die_profile.cpp)
add_library(i86::model ALIAS i86model)
target_include_directories(i86model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(i86model PUBLIC i86::spec PRIVATE yaml-cpp::yaml-cpp)
i86_target_defaults(i86model)

i86_add_unit_test(model_unit
  SOURCES tests/harness.cpp tests/model_test.cpp tests/bus_cycle_test.cpp tests/instruction_test.cpp
  LIBS i86::model)
