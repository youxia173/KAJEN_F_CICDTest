# Minimal static helper library for IKEA-style host unit tests.
cmake_minimum_required(VERSION 3.10)

add_library(homebrew_static STATIC stub.c)

target_include_directories(homebrew_static PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
)

set(DEFAULT_C_COMPILE_FLAGS -std=gnu11 -Wall -Wextra PARENT_SCOPE)
set(DEFAULT_LINK_FLAGS "" PARENT_SCOPE)
