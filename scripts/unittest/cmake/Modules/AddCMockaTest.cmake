# Minimal AddCMockaTest shim (API compatible with cmocka cmake modules).
function(add_cmocka_test _target)
  set(_options)
  set(_one_value_args)
  set(_multi_value_args SOURCES COMPILE_OPTIONS LINK_LIBRARIES)
  cmake_parse_arguments(_arg "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

  if(NOT _arg_SOURCES)
    message(FATAL_ERROR "add_cmocka_test(${_target}): SOURCES required")
  endif()

  add_executable(${_target} ${_arg_SOURCES})
  if(_arg_COMPILE_OPTIONS)
    target_compile_options(${_target} PRIVATE ${_arg_COMPILE_OPTIONS})
  endif()
  if(_arg_LINK_LIBRARIES)
    target_link_libraries(${_target} PRIVATE ${_arg_LINK_LIBRARIES})
  endif()
  add_test(NAME ${_target} COMMAND ${_target})
endfunction()

function(add_cmocka_test_environment _target)
  set_tests_properties(${_target} PROPERTIES ENVIRONMENT "CMOCKA_MESSAGE_OUTPUT=STDOUT")
endfunction()
