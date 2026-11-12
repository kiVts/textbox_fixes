cmake_minimum_required(VERSION 3.24)
project(text_fields LANGUAGES C CXX)

# Needs the SWML repo: cmake -B build -A x64 -DSWML_SDK_DIR=path/to/swml/sdk -DSWML_GAME_DATA_DIR=path/to/generated/headers
set(SWML_SDK_DIR "" CACHE PATH "SWML SDK folder (the one with SwmlMod.cmake)")
if(NOT EXISTS "${SWML_SDK_DIR}/SwmlMod.cmake")
  message(FATAL_ERROR "Set SWML_SDK_DIR to the SWML SDK folder")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_subdirectory("${SWML_SDK_DIR}" swml_sdk)

swml_add_mod(text_fields SOURCES
  src/setup.cpp
)
target_include_directories(text_fields PRIVATE src)

if(NOT EXISTS "${SWML_GAME_DATA_DIR}/native_text_field_signatures.h")
  message(FATAL_ERROR "Provide native_text_field_signatures.h in SWML_GAME_DATA_DIR")
endif()
