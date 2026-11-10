# Common compile settings for every first-party target.
function(i86_target_defaults target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${I86_WERROR}>:/WX>)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
                                             $<$<BOOL:${I86_WERROR}>:-Werror>)
  endif()
  if(I86_ASAN AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# i86_add_unit_test(<name> SOURCES ... LIBS ...)
# Builds a GoogleTest executable and registers each TEST() with ctest.
# Tests run with the repo root as working directory so they can open spec/ files.
function(i86_add_unit_test name)
  if(NOT I86_BUILD_TESTS)
    return()
  endif()
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  i86_target_defaults(${name})
  target_link_libraries(${name} PRIVATE ${ARG_LIBS} GTest::gtest_main GTest::gmock)
  target_compile_definitions(${name} PRIVATE I86_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtest_discover_tests(${name} WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                       PROPERTIES LABELS "unit")
endfunction()
