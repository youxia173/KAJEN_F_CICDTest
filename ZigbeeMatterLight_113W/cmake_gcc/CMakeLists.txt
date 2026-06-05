# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	ZigbeeMatterLight_113W
	VERSION 1.0
	LANGUAGES C CXX ASM
)

# Include the definition of the slc_ZigbeeMatterLight_113W target,
# which contains the content of the SLC project
include(ZigbeeMatterLight_113W.cmake)

# SM15135E driver sources only; SPIDRV is provided by the SLC project (ZigbeeMatterLight_113W.cmake).
target_sources(slc PRIVATE
    "../src/sm15135e.cpp"
    "../src/sm15135e_port.c"
)

add_executable(ZigbeeMatterLight_113W
    # Add additional sources here
)

target_include_directories(ZigbeeMatterLight_113W PUBLIC
    # Add additional include paths here
)

target_compile_definitions(ZigbeeMatterLight_113W PUBLIC
    # Add additional macros here
)

target_compile_options(ZigbeeMatterLight_113W PUBLIC
    # Set additional compiler flags here
)

target_link_options(ZigbeeMatterLight_113W PUBLIC
    # Set additional linker flags here
)

# Link with the content defined in the SLC project
target_link_libraries(ZigbeeMatterLight_113W PRIVATE
    slc
)

# Include managed project content if available
include(ZigbeeMatterLight_113W_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(ZigbeeMatterLight_113W PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET ZigbeeMatterLight_113W
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:ZigbeeMatterLight_113W>" "$<TARGET_FILE_DIR:ZigbeeMatterLight_113W>/$<TARGET_FILE_BASE_NAME:ZigbeeMatterLight_113W>.s37"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:ZigbeeMatterLight_113W>" "$<TARGET_FILE_DIR:ZigbeeMatterLight_113W>/$<TARGET_FILE_BASE_NAME:ZigbeeMatterLight_113W>.hex"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:ZigbeeMatterLight_113W>" "$<TARGET_FILE_DIR:ZigbeeMatterLight_113W>/$<TARGET_FILE_BASE_NAME:ZigbeeMatterLight_113W>.bin" 
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET ZigbeeMatterLight_113W
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()