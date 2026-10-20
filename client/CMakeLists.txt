cmake_minimum_required(VERSION 3.24)

project(BluboxClient VERSION 0.1.6 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Keep third-party demos and test targets out of the client build.
set(BUILD_EXAMPLES OFF CACHE BOOL "Build raylib examples" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "Build Jolt Hello World" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "Build Jolt performance test" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "Build Jolt samples" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "Build Jolt viewer" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "Build Jolt unit tests" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "Build Jolt without link-time optimization" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Use the standard dynamic MSVC runtime" FORCE)

FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.3.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build
)

FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.18
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(raylib JoltPhysics enet nlohmann_json)

add_executable(blubox_client
    src/ClientMain.cpp
    src/ClientSession.cpp
    src/NetworkClient.cpp
    src/LaunchOptions.cpp
    src/ProtocolLaunchData.cpp
    src/PhysicsWorld.cpp
    src/Player.cpp
    src/RaylibCompat.cpp
    src/GameClient.cpp
)

# Embed the licensed UI font in the client binary so the launcher can continue
# updating a single executable without a sidecar asset directory.
set(BLUBOX_UI_FONT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxUiFont.hpp")
add_custom_command(
    OUTPUT "${BLUBOX_UI_FONT_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Lato-Regular.ttf"
            "-DOUTPUT_FILE=${BLUBOX_UI_FONT_HEADER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Lato-Regular.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    VERBATIM
)
target_sources(blubox_client PRIVATE "${BLUBOX_UI_FONT_HEADER}")

target_include_directories(blubox_client PRIVATE
    src
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    ${enet_SOURCE_DIR}/include
)
target_link_libraries(blubox_client PRIVATE raylib Jolt enet)

if(MSVC)
    target_compile_options(blubox_client PRIVATE /W4 /permissive-)
else()
    target_compile_options(blubox_client PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32 AND MINGW)
    # Jolt propagates AVX2 flags to consumers. Keep Raylib's by-value Camera
    # copy inside a small non-AVX wrapper to avoid MinGW stack-alignment faults.
    set_source_files_properties(src/RaylibCompat.cpp PROPERTIES
        COMPILE_OPTIONS "-mno-avx;-mno-avx2;-mno-fma;-mno-f16c"
    )
endif()

if(WIN32)
    target_compile_definitions(blubox_client PRIVATE _WIN32_WINNT=0x0600)
    target_link_libraries(blubox_client PRIVATE winmm)
endif()
if(WIN32 AND MINGW)
    # Release builds must run on Windows machines without a MinGW installation.
    target_link_options(blubox_client PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# The bootstrapper only links Raylib; it does not load Jolt or the game scene.
add_executable(blubox_launcher
    src/LauncherMain.cpp
    src/Launcher.cpp
    src/LaunchOptions.cpp
    src/ProtocolLaunchData.cpp
    src/WindowsProtocolRegistration.cpp
)
target_include_directories(blubox_launcher PRIVATE src)
target_link_libraries(blubox_launcher PRIVATE raylib nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(blubox_launcher PRIVATE /W4 /permissive-)
else()
    target_compile_options(blubox_launcher PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_compile_definitions(blubox_launcher PRIVATE _WIN32_WINNT=0x0600)
    target_link_libraries(blubox_launcher PRIVATE winmm advapi32 shell32 winhttp bcrypt)
endif()
if(WIN32 AND MINGW)
    target_link_options(blubox_launcher PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Local mock release manifest and development payload for the bootstrapper.
configure_file(version.json version.json COPYONLY)
add_custom_command(TARGET blubox_launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/version.json $<TARGET_FILE_DIR:blubox_launcher>/version.json
    VERBATIM
)
add_dependencies(blubox_launcher blubox_client)

