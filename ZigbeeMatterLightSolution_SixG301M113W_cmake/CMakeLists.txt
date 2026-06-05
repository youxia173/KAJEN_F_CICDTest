# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(ZigbeeMatterLightSolution_SixG301M113W)

include(ExternalProject)

ExternalProject_Add(ZigbeeMatterLight_113W
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../ZigbeeMatterLight_113W/cmake_gcc
  BINARY_DIR ${CMAKE_CURRENT_LIST_DIR}/../ZigbeeMatterLight_113W/cmake_gcc/build
  CMAKE_ARGS --preset project
  BUILD_COMMAND ${CMAKE_COMMAND} --build . --config $<CONFIG>
  INSTALL_COMMAND ""
  BUILD_ALWAYS true
  USES_TERMINAL_BUILD 1
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../ZigbeeMatterLight_113W/cmake_gcc
)
ExternalProject_Add(Matter-Bootloader_113W
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../Matter-Bootloader_113W/cmake_gcc
  BINARY_DIR ${CMAKE_CURRENT_LIST_DIR}/../Matter-Bootloader_113W/cmake_gcc/build
  CMAKE_ARGS --preset project
  BUILD_COMMAND ${CMAKE_COMMAND} --build . --config $<CONFIG>
  INSTALL_COMMAND ""
  BUILD_ALWAYS true
  USES_TERMINAL_BUILD 1
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../Matter-Bootloader_113W/cmake_gcc
)

ExternalProject_Add_StepDependencies(Matter-Bootloader_113W build ZigbeeMatterLight_113W)


set(post_build_command ${POST_BUILD_EXE} postbuild "./ZigbeeMatterLightSolution_SixG301M113W.slpb")


add_custom_target(postbuild ALL
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)

add_dependencies(postbuild ZigbeeMatterLight_113W Matter-Bootloader_113W)

# BEGIN_SIMPLICITY_STUDIO_METADATA=eJy9U91r2zAQ/1eEKaN9qBUvsIfMW9maMQx2U6aUfWAwinyNtck6z5LrjNH/vdc6cTqywJ72eOff153OvwORZNdpcpksvxZieTNPFoVYpDfLZHFVZIv5hzSYBfHFpjbsDlqn0b7Jgyic5AEDq7DUdk2Nd+IySfLg4m1u40yg6TwBHSOWdbON0wSpvG9mnPd9H/bTENs1fzmZRPxLlgpVQS3PtXVeWgWkTIyZxStZg2ukggGQopJ+8H+m5bSRKxcqrLlzfGcdgsKWlCgPY7Hbdp8qqneoDMvOwCe4hZZmAcd0SeLOqLDH9seTdVhKL/OAj9R5NodbbfUj/zh+CydCg86vOm1K4aEZ2/Rh3zw5pfS1tCW0Z2zEsxc/O/SvT0536c/4N71eAWTSe2hTva78bpBC6M3H6STKomj6OXSmWQ3kmB9ax/zvkeKmxe+gPLO0dhrrwKx4FKfHMft34BXWwCtpafO6boxW2v8Svis18rtXxbiVf0x+CNt68qMpB+z5e0RvUNIG/0PKY57jkfA/rmS4Qf7sCGO+/0eoDO4fAMWzN7A==END_SIMPLICITY_STUDIO_METADATA