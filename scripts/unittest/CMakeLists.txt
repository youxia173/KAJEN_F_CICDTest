
project(z_unit_test LANGUAGES C)
cmake_minimum_required(VERSION 3.10.2)
include(FindPkgConfig)
include(CMakePackageConfigHelpers)

message("ROOT_DIR=${ROOT_DIR}")

set(VS_ROOT ${ROOT_DIR} CACHE STRING "Value stream root path")
set(VS_COMPONENTS ${VS_ROOT}/kt_components CACHE STRING "Value stream default components path")
set(SDK_ROOT ${ROOT_DIR} CACHE STRING "SDK root path")
set(KT_COMPONENTS ${SDK_ROOT}/kt_components CACHE STRING "Keetat components path")
set(THIRD_PARTY ${SDK_ROOT}/sdk/third_party CACHE STRING "Homebrew third party path")
set(SILABS_SDK ${THIRD_PARTY}/silabs/gecko_sdk CACHE STRING "Silabs SDK path")
set(PLATFORM_UNITTEST ${SDK_ROOT}/platform/unittest CACHE STRING "Platform unittest path")

set(UNITTEST_DEPS ${PLATFORM_UNITTEST}/add_test_deps bin_1 CACHE STRING "Platform unittest path")
set(UNITTEST_RUN ${PLATFORM_UNITTEST}/run_test bin_2 CACHE STRING "Platform unittest path")


# Add code coverage to build
SET(GCC_COVERAGE_COMPILE_FLAGS "-fprofile-arcs -ftest-coverage")
SET(GCC_COVERAGE_LINK_FLAGS    "-lgcov")
#SET(GCC_COVERAGE_ASAN_FLAGS "-fsanitize=address")
SET(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}")
#SET(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} ${GCC_COVERAGE_ASAN_FLAGS}")
SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS}")


# Add compile flags to build
# SET(GCC_UNITTEST_COMPILE_FLAGS "-Wall -Werror")
SET(GCC_UNITTEST_COMPILE_FLAGS "-w")
SET(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} ${GCC_UNITTEST_COMPILE_FLAGS}")

# Setup cmocka
find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA REQUIRED IMPORTED_TARGET cmocka)
list(APPEND CMAKE_MODULE_PATH "../../third_party/cmocka/cmake/Modules")
include(AddCMockaTest)
# Add component dirs here
set(COMPONENT_DIRS
    ${ROOT_DIR}/kt_components
)

if("${RUN_SDK_TESTS}" STREQUAL "1")
    set(COMPONENT_DIRS ${COMPONENT_DIRS}
    )
else()
    message("SDK test cases are excluded.")
endif()

# Build static test lib
add_subdirectory(${SDK_ROOT}/unit_test/homebrew_static_lib homebrew_static)


# Macro for finding test case directories
MACRO(FIND_TEST_DIRECTORIES return_list subdir)
    FILE(GLOB_RECURSE new_list LIST_DIRECTORIES true ${subdir}/*/test/*)
    SET(dir_list "")
    FOREACH(file_path ${new_list})
       # MESSAGE( STATUS "File path " ${file_path} )
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

# Add all found test directories to unit test build
foreach(subdir ${COMPONENT_DIRS})
    if(IS_DIRECTORY ${subdir})
        SET(test_list "")
        FIND_TEST_DIRECTORIES(test_list ${subdir})
        foreach(testfile ${test_list})
            SET(test_found true)
            SET(TEST_NAME "")

            #Find the name of the test in the path
            string(REPLACE "/" ";" TEST_PATH_LIST ${testfile})
            list(LENGTH TEST_PATH_LIST len)
            MATH(EXPR len "${len}-2")
            list(GET TEST_PATH_LIST ${len} TEST_NAME)

            #Apply filter if any
            if(NOT ${FILTER} STREQUAL "")
                string(COMPARE EQUAL  ${TEST_NAME} ${FILTER} test_found)
            endif()
            if(test_found)
                # Add the found testname as build directory
                MESSAGE( STATUS "Including unittest dir " ${testfile} " " ${TEST_NAME} )
                add_subdirectory(${testfile} ${TEST_NAME})
            endif()
        endforeach()
    endif()
endforeach()
