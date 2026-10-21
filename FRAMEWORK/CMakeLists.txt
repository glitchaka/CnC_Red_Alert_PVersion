add_library(ra_engine STATIC
    src/Engine.cpp
    src/RTS.cpp
    src/Net.cpp
)
add_library(RA::Engine ALIAS ra_engine)

target_include_directories(ra_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(ra_engine PUBLIC cxx_std_17)

if(WIN32)
    target_link_libraries(ra_engine PUBLIC SDL3::SDL3-static SDL3_image::SDL3_image-static ws2_32)
else()
    target_link_libraries(ra_engine PUBLIC SDL3::SDL3-static SDL3_image::SDL3_image-static)
endif()

if(MSVC)
    target_compile_options(ra_engine PRIVATE /W4 /permissive-)
endif()

set_target_properties(ra_engine PROPERTIES
    OUTPUT_NAME "ra_engine"
)

if(RA_BUILD_SANDBOX)
    add_executable(ra_sandbox
        ${CMAKE_SOURCE_DIR}/examples/click_command.generated.cpp
    )
    target_link_libraries(ra_sandbox PRIVATE RA::Engine)
    if(WIN32)
        target_link_libraries(ra_sandbox PRIVATE user32 winhttp)
    endif()
    set_target_properties(ra_sandbox PROPERTIES
        OUTPUT_NAME "ra_sandbox"
    )
endif()
