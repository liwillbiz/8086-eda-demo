# Test ROMs (design doc §6.1, decision D7): NASM `cpu 8086`, flat binaries, embedded into
# i86testprog together with the .pat patterns.
option(I86_REQUIRE_NASM "Fail the configure step if NASM is missing" OFF)
find_program(NASM_EXECUTABLE nasm)
if(NOT NASM_EXECUTABLE)
  if(I86_REQUIRE_NASM)
    message(FATAL_ERROR "NASM not found; install it (apt install nasm / brew install nasm).")
  endif()
  message(WARNING "NASM not found: test ROMs are not built, so ROM-based tests report Error.")
endif()

set(I86_ROM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/roms)
file(GLOB I86_ROM_SOURCES CONFIGURE_DEPENDS ${I86_ROM_DIR}/*.asm)
file(GLOB I86_ROM_INCLUDES CONFIGURE_DEPENDS ${I86_ROM_DIR}/inc/*.inc)
file(GLOB I86_PATTERNS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/patterns/*.pat)

set(rom_entries "")
set(rom_bins "")
if(NASM_EXECUTABLE)
  foreach(asm IN LISTS I86_ROM_SOURCES)
    get_filename_component(stem ${asm} NAME_WE)
    set(bin ${CMAKE_CURRENT_BINARY_DIR}/roms/${stem}.bin)
    add_custom_command(
      OUTPUT ${bin}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/roms
      COMMAND ${NASM_EXECUTABLE} -f bin -w+all -I ${I86_ROM_DIR}/inc/
              -o ${bin} -l ${CMAKE_CURRENT_BINARY_DIR}/roms/${stem}.lst ${asm}
      DEPENDS ${asm} ${I86_ROM_INCLUDES}
      COMMENT "NASM ${stem}.asm"
      VERBATIM)
    list(APPEND rom_bins ${bin})
    list(APPEND rom_entries "${stem}=${bin}")
  endforeach()
endif()
set(pat_entries "")
foreach(pat IN LISTS I86_PATTERNS)
  get_filename_component(stem ${pat} NAME_WE)
  list(APPEND pat_entries "${stem}=${pat}")
endforeach()

set(I86_EMBEDDED_CPP ${CMAKE_CURRENT_BINARY_DIR}/embedded.cpp)
if(NASM_EXECUTABLE)
  set(nasm_found ON)
else()
  set(nasm_found OFF)
endif()
add_custom_command(
  OUTPUT ${I86_EMBEDDED_CPP}
  COMMAND ${CMAKE_COMMAND} -DOUT=${I86_EMBEDDED_CPP} "-DROMS=${rom_entries}"
          "-DPATTERNS=${pat_entries}" -DNASM_FOUND=${nasm_found}
          -P ${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake
  DEPENDS ${rom_bins} ${I86_PATTERNS} ${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake
  COMMENT "Embedding test ROMs and patterns"
  VERBATIM)

add_library(i86testprog
  src/registry.cpp
  src/runner.cpp
  src/rom_case.cpp
  src/functional.cpp
  src/protocol.cpp
  src/margin.cpp
  src/shmoo.cpp
  report/datalog.cpp
  report/summary.cpp
  report/shmoo.cpp
  ${I86_EMBEDDED_CPP})
add_library(i86::testprog ALIAS i86testprog)
target_include_directories(i86testprog
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(i86testprog PUBLIC i86::tester i86::golden i86::spec)
i86_target_defaults(i86testprog)

# Object library so REGISTER_TEST static initializers are never dropped by the linker.
file(GLOB_RECURSE I86_CASE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cases/*.cpp)
add_library(i86cases OBJECT ${I86_CASE_SOURCES})
target_include_directories(i86cases PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(i86cases PUBLIC i86::testprog)
i86_target_defaults(i86cases)

add_executable(i86test cli/main.cpp)
target_link_libraries(i86test PRIVATE i86cases i86::testprog)
target_include_directories(i86test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
i86_target_defaults(i86test)

i86_add_unit_test(testprog_unit
  SOURCES tests/smoke_test.cpp tests/harness_test.cpp tests/cases_test.cpp
          tests/differential_fuzz_test.cpp tests/report_test.cpp tests/margin_test.cpp
  LIBS i86cases i86::testprog)
if(TARGET testprog_unit)
  target_include_directories(testprog_unit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()
