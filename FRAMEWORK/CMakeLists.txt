add_library(ra_engine STATIC
    src/Engine.cpp
    src/RTS.cpp
)
add_library(RA::Engine ALIAS ra_engine)

target_include_directories(ra_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(ra_engine PUBLIC cxx_std_17)
target_link_libraries(ra_engine PUBLIC SDL3::SDL3-static SDL3_image::SDL3_image-static)

if(MSVC)
    target_compile_options(ra_engine PRIVATE /W4 /permissive-)
endif()

set_target_properties(ra_engine PROPERTIES
    OUTPUT_NAME "ra_engine"
)

if(RA_BUILD_SANDBOX)
    add_executable(ra_sandbox
        ${CMAKE_SOURCE_DIR}/examples/basic_game.cpp
    )
    target_link_libraries(ra_sandbox PRIVATE RA::Engine)
    set_target_properties(ra_sandbox PROPERTIES
        OUTPUT_NAME "ra_sandbox"
    )

    add_custom_command(TARGET ra_sandbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ra_sandbox>/assets/kenney"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RA_KENNEY_HUD}"
            "$<TARGET_FILE_DIR:ra_sandbox>/assets/kenney/hud_bar.png"
        COMMENT "Copying CC0 Kenney HUD sample asset"
    )
endif()
