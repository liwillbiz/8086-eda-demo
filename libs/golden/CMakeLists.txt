# Links only the header-only i86::common: the golden emulator must stay
# independent of libs/model so the two cannot share a common-mode bug.
add_library(i86golden src/emulator.cpp src/cpu.cpp src/rom.cpp)
add_library(i86::golden ALIAS i86golden)
target_include_directories(i86golden PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(i86golden PUBLIC i86::common)
i86_target_defaults(i86golden)

i86_add_unit_test(golden_unit SOURCES tests/golden_test.cpp tests/flags_table_test.cpp LIBS i86::golden)
