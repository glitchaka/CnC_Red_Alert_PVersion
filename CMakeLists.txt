cmake_minimum_required(VERSION 3.25)
project(RedAlertFramework VERSION 0.2.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The current framework port targets Windows only.")
endif()

option(RA_BUILD_SANDBOX "Build the framework sandbox example" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

include(FetchContent)

# SDL3: windowing, input, timing, rendering and audio.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

# SDL_image: PNG loader for the real Mystic Woods / Bit Bonanza assets.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_GIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JPG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_LBM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PCX OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_QOI OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SVG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TGA OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XCF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XPM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XV OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3_image
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
    GIT_TAG release-3.4.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3_image)

# Kenney CC0 sample HUD asset retained as part of the framework sample assets.
if(RA_BUILD_SANDBOX)
    set(RA_SAMPLE_ASSET_DIR "${CMAKE_BINARY_DIR}/sample_assets")
    file(MAKE_DIRECTORY "${RA_SAMPLE_ASSET_DIR}/kenney")
    set(RA_KENNEY_HUD "${RA_SAMPLE_ASSET_DIR}/kenney/hud_bar.png")
    if(NOT EXISTS "${RA_KENNEY_HUD}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/shorepine/kenney/main/ui/UI%20Pack%20-%20Sci-fi/Blue/bar_round_gloss_large.png"
            "${RA_KENNEY_HUD}"
            EXPECTED_HASH SHA256=3c5ba6a36ec990665df901c42e1148fc4f5128858fc86514e667a69a03f34103
            TLS_VERIFY ON
            STATUS RA_HUD_DOWNLOAD_STATUS
        )
        list(GET RA_HUD_DOWNLOAD_STATUS 0 RA_HUD_DOWNLOAD_CODE)
        if(NOT RA_HUD_DOWNLOAD_CODE EQUAL 0)
            list(GET RA_HUD_DOWNLOAD_STATUS 1 RA_HUD_DOWNLOAD_MESSAGE)
            message(FATAL_ERROR "Failed to download CC0 Kenney HUD sample: ${RA_HUD_DOWNLOAD_MESSAGE}")
        endif()
    endif()
endif()

add_subdirectory(FRAMEWORK)
