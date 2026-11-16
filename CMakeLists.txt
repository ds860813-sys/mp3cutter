cmake_minimum_required(VERSION 3.24)
project(Mp3cutter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED BASS_DIR)
    set(BASS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Bass")
endif()
get_filename_component(BASS_DIR "${BASS_DIR}" ABSOLUTE)

set(BASS_CORE_DIR "${BASS_DIR}/bass24")
set(BASS_MIX_DIR "${BASS_DIR}/bassmix24")
set(BASS_WASAPI_DIR "${BASS_DIR}/basswasapi24")

foreach(REQUIRED_FILE
    "${BASS_CORE_DIR}/c/bass.h"
    "${BASS_CORE_DIR}/c/x64/bass.lib"
    "${BASS_CORE_DIR}/x64/bass.dll"
    "${BASS_MIX_DIR}/c/bassmix.h"
    "${BASS_MIX_DIR}/c/x64/bassmix.lib"
    "${BASS_MIX_DIR}/x64/bassmix.dll"
    "${BASS_WASAPI_DIR}/c/basswasapi.h"
    "${BASS_WASAPI_DIR}/c/x64/basswasapi.lib"
    "${BASS_WASAPI_DIR}/x64/basswasapi.dll"
)
    if(NOT EXISTS "${REQUIRED_FILE}")
        message(FATAL_ERROR "Required BASS file not found: ${REQUIRED_FILE}")
    endif()
endforeach()

add_executable(Mp3cutter WIN32 main.cpp app.rc)
target_compile_options(Mp3cutter PRIVATE /W4 /permissive- /utf-8 /guard:cf)
target_compile_definitions(Mp3cutter PRIVATE UNICODE _UNICODE)
target_link_options(Mp3cutter PRIVATE /GUARD:CF /DYNAMICBASE /NXCOMPAT)
target_include_directories(Mp3cutter PRIVATE
    "${BASS_CORE_DIR}/c"
    "${BASS_MIX_DIR}/c"
    "${BASS_WASAPI_DIR}/c"
)
target_link_libraries(Mp3cutter PRIVATE
    "${BASS_CORE_DIR}/c/x64/bass.lib"
    "${BASS_MIX_DIR}/c/x64/bassmix.lib"
    "${BASS_WASAPI_DIR}/c/x64/basswasapi.lib"
    comctl32 comdlg32 dwmapi uxtheme user32 gdi32 shell32
)
set_property(TARGET Mp3cutter PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_target_properties(Mp3cutter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

add_custom_command(TARGET Mp3cutter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BASS_CORE_DIR}/x64/bass.dll"
        "${BASS_MIX_DIR}/x64/bassmix.dll"
        "${BASS_WASAPI_DIR}/x64/basswasapi.dll"
        "$<TARGET_FILE_DIR:Mp3cutter>"
)
