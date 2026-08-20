set(TEST_NAME kajen_level_util)

project(cmocka-${TEST_NAME} C)
set(TEST_LIBRARIES homebrew_static PkgConfig::CMOCKA)
set(COMPONENT_ROOT ..)

include_directories(
  ${COMPONENT_ROOT}/inc
)

set(TEST_SOURCES
  ${COMPONENT_ROOT}/test/kajen_level_util_test.c
  ${COMPONENT_ROOT}/src/kajen_level_util.c
)

add_cmocka_test(${TEST_NAME}
  SOURCES ${TEST_SOURCES}
  COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
  LINK_LIBRARIES ${TEST_LIBRARIES})

add_cmocka_test_environment(${TEST_NAME})

target_link_libraries(${TEST_NAME} PRIVATE m)
