cmake_minimum_required(VERSION 3.25)
project(RedAlertPVersion LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This port currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Build this repository's own source tree. Legacy assembler modules and
# obsolete middleware are deliberately isolated and will be ported/replaced
# as the modernisation progresses.
file(GLOB RA_CODE CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/CODE/*.CPP"
    "${CMAKE_CURRENT_SOURCE_DIR}/WIN32LIB/*.CPP"
    "${CMAKE_CURRENT_SOURCE_DIR}/IPX/*.CPP"
    "${CMAKE_CURRENT_SOURCE_DIR}/VQ/*.CPP"
    "${CMAKE_CURRENT_SOURCE_DIR}/WINVQ/*.CPP"
)

# Backup/reference files are not compilation units.
list(FILTER RA_CODE EXCLUDE REGEX "\\.BAK$")

add_executable(redalert WIN32 ${RA_CODE})

target_include_directories(redalert PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/CODE"
    "${CMAKE_CURRENT_SOURCE_DIR}/WIN32LIB"
    "${CMAKE_CURRENT_SOURCE_DIR}/IPX"
    "${CMAKE_CURRENT_SOURCE_DIR}/VQ"
    "${CMAKE_CURRENT_SOURCE_DIR}/WINVQ"
)

target_compile_definitions(redalert PRIVATE
    WIN32=1
    _WINDOWS=1
    ENGLISH=1
    FRENCH=0
    GERMAN=0
    NOMINMAX=1
    WIN32_LEAN_AND_MEAN=1
)

if(MSVC)
    target_compile_options(redalert PRIVATE
        /permissive-
        /Zc:__cplusplus
        /W3
        /wd4244
        /wd4267
        /wd4996
    )
endif()

# Native Win32 system libraries already used by the original RA95 code.
target_link_libraries(redalert PRIVATE
    user32
    gdi32
    advapi32
    winmm
    ws2_32
    shell32
    ole32
    uuid
)

set_target_properties(redalert PROPERTIES
    OUTPUT_NAME "RedAlert"
)
