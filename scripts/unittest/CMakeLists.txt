
project(z_unit_test LANGUAGES C)
cmake_minimum_required(VERSION 3.10.2)
include(FindPkgConfig)
include(CMakePackageConfigHelpers)

message("ROOT_DIR=${ROOT_DIR}")

set(VS_ROOT ${ROOT_DIR} CACHE STRING "Value stream root path")
set(VS_COMPONENTS ${VS_ROOT}/kt_components CACHE STRING "Value stream default components path")
set(SDK_ROOT ${ROOT_DIR} CACHE STRING "SDK root path")
set(KT_COMPONENTS ${SDK_ROOT}/kt_components CACHE STRING "Keetat components path")

enable_testing()

# Add code coverage to build
SET(GCC_COVERAGE_COMPILE_FLAGS "-fprofile-arcs -ftest-coverage")
SET(GCC_COVERAGE_LINK_FLAGS    "-lgcov")
SET(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}")
SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS}")

# Add compile flags to build
SET(GCC_UNITTEST_COMPILE_FLAGS "-w")
SET(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} ${GCC_UNITTEST_COMPILE_FLAGS}")

# Setup cmocka (system package on CI runner)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA REQUIRED IMPORTED_TARGET cmocka)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")
include(AddCMockaTest)

set(COMPONENT_DIRS
    ${ROOT_DIR}/kt_components
)

if("${RUN_SDK_TESTS}" STREQUAL "1")
    message(STATUS "SDK unit tests enabled")
else()
    message("SDK test cases are excluded.")
endif()

add_subdirectory(${SDK_ROOT}/unit_test/homebrew_static_lib homebrew_static)

MACRO(FIND_TEST_DIRECTORIES return_list subdir)
    FILE(GLOB_RECURSE new_list LIST_DIRECTORIES true ${subdir}/*/test/*)
    SET(dir_list "")
    FOREACH(file_path ${new_list})
        GET_FILENAME_COMPONENT(dir_path ${file_path} PATH)
        if(IS_DIRECTORY ${dir_path})
            if (EXISTS ${dir_path}/CMakeLists.txt)
                SET(dir_list ${dir_list} ${dir_path})
            endif()
        endif()
    ENDFOREACH()
    LIST(REMOVE_DUPLICATES dir_list)
    SET(${return_list} ${dir_list})
ENDMACRO()

foreach(subdir ${COMPONENT_DIRS})
    if(IS_DIRECTORY ${subdir})
        SET(test_list "")
        FIND_TEST_DIRECTORIES(test_list ${subdir})
        foreach(testfile ${test_list})
            SET(test_found true)
            SET(TEST_NAME "")

            string(REPLACE "/" ";" TEST_PATH_LIST ${testfile})
            list(LENGTH TEST_PATH_LIST len)
            MATH(EXPR len "${len}-2")
            list(GET TEST_PATH_LIST ${len} TEST_NAME)

            if(NOT "${FILTER}" STREQUAL "")
                string(COMPARE EQUAL ${TEST_NAME} ${FILTER} test_found)
            endif()
            if(test_found)
                MESSAGE(STATUS "Including unittest dir ${testfile} ${TEST_NAME}")
                add_subdirectory(${testfile} ${TEST_NAME})
            endif()
        endforeach()
    endif()
endforeach()
