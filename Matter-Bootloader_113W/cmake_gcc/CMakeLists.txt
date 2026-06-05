# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	Matter-Bootloader_113W
	VERSION 1.0
	LANGUAGES C CXX ASM
)

# Include the definition of the slc_Matter-Bootloader_113W target,
# which contains the content of the SLC project
include(Matter-Bootloader_113W.cmake)

add_executable(Matter-Bootloader_113W
    # Add additional sources here
)

target_include_directories(Matter-Bootloader_113W PUBLIC
    # Add additional include paths here
)

target_compile_definitions(Matter-Bootloader_113W PUBLIC
    # Add additional macros here
)

target_compile_options(Matter-Bootloader_113W PUBLIC
    # Set additional compiler flags here
)

target_link_options(Matter-Bootloader_113W PUBLIC
    # Set additional linker flags here
)

# Link with the content defined in the SLC project
target_link_libraries(Matter-Bootloader_113W PRIVATE
    slc
)

# Include managed project content if available
include(Matter-Bootloader_113W_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(Matter-Bootloader_113W PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET Matter-Bootloader_113W
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:Matter-Bootloader_113W>" "$<TARGET_FILE_DIR:Matter-Bootloader_113W>/$<TARGET_FILE_BASE_NAME:Matter-Bootloader_113W>.s37"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:Matter-Bootloader_113W>" "$<TARGET_FILE_DIR:Matter-Bootloader_113W>/$<TARGET_FILE_BASE_NAME:Matter-Bootloader_113W>.hex"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:Matter-Bootloader_113W>" "$<TARGET_FILE_DIR:Matter-Bootloader_113W>/$<TARGET_FILE_BASE_NAME:Matter-Bootloader_113W>.bin" 
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET Matter-Bootloader_113W
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()