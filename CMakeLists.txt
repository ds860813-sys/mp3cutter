cmake_minimum_required(VERSION 3.24)
project(Mp3cutter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED BASS_SDK)
    get_filename_component(BASS_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../bass24" ABSOLUTE)
endif()

add_executable(Mp3cutter WIN32 main.cpp app.rc)
target_compile_options(Mp3cutter PRIVATE /W4 /permissive- /utf-8 /guard:cf)
target_compile_definitions(Mp3cutter PRIVATE UNICODE _UNICODE)
target_link_options(Mp3cutter PRIVATE /GUARD:CF /DYNAMICBASE /NXCOMPAT)
target_include_directories(Mp3cutter PRIVATE
    "${BASS_SDK}"
    "${BASS_SDK}/Additions/bassmix24/c"
    "${BASS_SDK}/Additions/basswasapi24/c"
)
target_link_libraries(Mp3cutter PRIVATE
    "${BASS_SDK}/bass.lib"
    "${BASS_SDK}/Additions/bassmix24/c/x64/bassmix.lib"
    "${BASS_SDK}/Additions/basswasapi24/c/x64/basswasapi.lib"
    comctl32 comdlg32 dwmapi uxtheme user32 gdi32 shell32
)
set_property(TARGET Mp3cutter PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_custom_command(TARGET Mp3cutter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BASS_SDK}/bin/x64/Release/bass.dll"
        "${BASS_SDK}/Additions/bassmix24/x64/bassmix.dll"
        "${BASS_SDK}/Additions/basswasapi24/x64/basswasapi.dll"
        "$<TARGET_FILE_DIR:Mp3cutter>"
)
