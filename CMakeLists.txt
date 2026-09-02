cmake_minimum_required(VERSION 3.15)

project(Coinop
    VERSION 1.0.5
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Universal by default so the bundle loads in both Apple Silicon and Intel
# builds of Resolume.
#
# This must sit immediately after project() and before any target exists. CMake
# latches the architecture list when the first target is created and silently
# ignores it if set later -- the result is an arm64-only binary that the build
# log calls a success. Verified with lipo in tools/verify.sh, never with the log.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Apple deprecated the whole OpenGL API in 10.14 and FFGL is an OpenGL
    # interface, so every SDK call warns. There is no alternative API to move to.
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

if(WIN32)
    # windows.h defines min and max as macros, which turns every std::min into
    # `std::(...)` and fails with an error that names neither. Only ever seen in
    # CI, after macOS has already gone green.
    add_compile_definitions(NOMINMAX)
endif()

option(COINOP_BUILD_TOOLS "Build the offline simulation harness" ON)
option(COINOP_BUILD_PLUGINS "Build the FFGL plugins (needs the SDK submodule)" ON)

# ---------------------------------------------------------------------------
# The simulation, as a library that knows nothing about FFGL or OpenGL.
#
# This separation is the point, not tidiness. Every game, the grid, the
# rasteriser, the input queue and the fixed-timestep driver link with no
# graphics API and no host SDK present -- which is what lets `coinoptest` drive
# the real game classes, in a plain command-line binary, with no GL context and
# no window server. The alternative is verifying a game by rendering it and
# counting pixels, which is slower, flakier, and cannot tell you the snake is
# fourteen long.
#
# If this target ever fails to link without ffgl::sdk, something has reached
# from the sim into the plugin and the harness is about to become a GL app.
# ---------------------------------------------------------------------------
add_library(coinop_sim STATIC
    source/Game.h
    source/Game.cpp
    source/Grid.h
    source/Raster.h
    source/Rng.h
    source/Input.h
    source/Vec.h
    source/Sim.h
    source/Sim.cpp
    source/games/Snake.h
    source/games/Snake.cpp
    source/games/Bricks.h
    source/games/Bricks.cpp
    source/games/Marchers.h
    source/games/Marchers.cpp
    source/games/Rally.h
    source/games/Rally.cpp
    source/games/Drift.h
    source/games/Drift.cpp
    source/games/Stacker.h
    source/games/Stacker.cpp
    source/games/Chase.h
    source/games/Chase.cpp
    source/games/Girders.h
    source/games/Girders.cpp
    source/games/Swarm.h
    source/games/Swarm.cpp
    source/games/Trails.h
    source/games/Trails.cpp
    source/games/Reflex.h
    source/games/Reflex.cpp
    source/games/Rafters.h
    source/games/Rafters.cpp
    source/games/Duel.h
    source/games/Duel.cpp
    source/games/Flapper.h
    source/games/Flapper.cpp
)

target_include_directories(coinop_sim PUBLIC source)
target_compile_definitions(coinop_sim PUBLIC COINOP_VERSION="${PROJECT_VERSION}")
set_target_properties(coinop_sim PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Offline harness. Asserts on simulation state, not on pixels.
# ---------------------------------------------------------------------------
if(COINOP_BUILD_TOOLS)
    add_executable(coinoptest tools/coinoptest/main.cpp)
    target_link_libraries(coinoptest PRIVATE coinop_sim)

    # The other half: the shader, in a headless CGL context. Separate binary
    # because coinoptest deliberately links no graphics API at all, and the
    # moment it did, the guarantee that the sim is GL-free would be gone.
    if(APPLE)
        add_executable(coinopgl tools/coinopgl/main.cpp source/Shaders.cpp source/Controls.cpp)
        target_link_libraries(coinopgl PRIVATE coinop_sim
            "-framework OpenGL"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# The FFGL SDK, as a git submodule under external/.
# ---------------------------------------------------------------------------
if(COINOP_BUILD_PLUGINS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl/CMakeLists.txt")
        message(FATAL_ERROR
            "FFGL SDK not found. Run: git submodule update --init --recursive")
    endif()

    set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/ffgl EXCLUDE_FROM_ALL)

    # -----------------------------------------------------------------------
    # Everything both plugins share, as an OBJECT library.
    #
    # OBJECT and not STATIC, and the same trap as the rest of the fleet:
    # CFFGLPluginInfo registers itself from a file-scope constructor and nothing
    # ever references that object by name, so in an archive the linker is
    # entitled to drop the whole translation unit. The result is a bundle that
    # loads, exports plugMain, and reports that it contains no plugins.
    #
    # Note what is NOT in here: SourcePlugin.cpp and EffectPlugin.cpp. Each
    # carries its own CFFGLPluginInfo with its own plugin ID, and each is listed
    # directly in its own MODULE target. Putting either in the shared library
    # would register both plugins into both bundles.
    # -----------------------------------------------------------------------
    add_library(coinop_core OBJECT
        source/Diag.h
        source/Diag.cpp
        source/Controls.h
        source/Controls.cpp
        source/Shaders.h
        source/Shaders.cpp
        source/Coinop.h
        source/Coinop.cpp
    )

    target_include_directories(coinop_core PUBLIC source)
    target_link_libraries(coinop_core PUBLIC coinop_sim ffgl::sdk)
    set_target_properties(coinop_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

    if(NOT APPLE)
        find_package(GLEW REQUIRED)
        target_link_libraries(coinop_core PUBLIC GLEW::GLEW)
    endif()

    # -----------------------------------------------------------------------
    # The two plugins. FFGL resolves one plugMain per binary, so a source and an
    # effect are two bundles -- not one bundle with two entries.
    #
    # Thirteen games are NOT thirteen bundles: the game is a dropdown. See
    # Game.h for what that arrangement costs and what it bought.
    # -----------------------------------------------------------------------
    function(coinop_add_plugin target entry bundle_name identifier)
        add_library(${target} MODULE ${entry})
        target_link_libraries(${target} PRIVATE coinop_core)

        if(APPLE)
            set_target_properties(${target} PROPERTIES
                BUNDLE TRUE
                BUNDLE_EXTENSION "bundle"
                OUTPUT_NAME "${bundle_name}"
                MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
                MACOSX_BUNDLE_BUNDLE_NAME "${bundle_name}"
                MACOSX_BUNDLE_GUI_IDENTIFIER "${identifier}"
                MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            )
            # FFGL hosts resolve plugMain by symbol name, so keep default
            # visibility.
            target_link_libraries(${target} PRIVATE
                "-framework OpenGL"
                "-framework Carbon"
                "-framework AppKit"
            )
        else()
            set_target_properties(${target} PROPERTIES OUTPUT_NAME "${bundle_name}")
        endif()
    endfunction()

    coinop_add_plugin(CoinopSource source/SourcePlugin.cpp
        "Coinop" "com.stoatworks.ffgl.coinop")
    coinop_add_plugin(CoinopEffect source/EffectPlugin.cpp
        "Coinop Over" "com.stoatworks.ffgl.coinop.over")

    # -----------------------------------------------------------------------
    # `cmake --install` drops both bundles into the user's Resolume folder.
    # -----------------------------------------------------------------------
    if(APPLE AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Documents/Resolume Arena/Extra Effects"
            CACHE PATH "" FORCE)
    endif()

    if(APPLE)
        install(TARGETS CoinopSource CoinopEffect
            LIBRARY DESTINATION "."
            BUNDLE DESTINATION ".")
    endif()
endif()
