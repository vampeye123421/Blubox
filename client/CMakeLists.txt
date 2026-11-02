cmake_minimum_required(VERSION 3.24)

project(BluboxClient VERSION 0.2.28 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
elseif(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(BLUBOX_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/bloxity-icon.ico")
set(BLUBOX_APP_ICON_PATH "${BLUBOX_APP_ICON}")
file(TO_CMAKE_PATH "${BLUBOX_APP_ICON_PATH}" BLUBOX_APP_ICON_PATH)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(cmake/BluboxIcon.rc.in "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxIcon.rc" @ONLY)

# Keep third-party demos and test targets out of the client build.
set(BUILD_EXAMPLES OFF CACHE BOOL "Build raylib examples" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "Build Jolt Hello World" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "Build Jolt performance test" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "Build Jolt samples" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "Build Jolt viewer" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "Build Jolt unit tests" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "Build Jolt without link-time optimization" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Use the standard dynamic MSVC runtime" FORCE)
set(LUAU_BUILD_CLI OFF CACHE BOOL "Do not build Luau command line tools" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "Do not build Luau tests" FORCE)

set(BUILD_CURL_EXE OFF CACHE BOOL "Do not build curl command line tools" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Do not build curl tests" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "Do not build curl documentation" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "Do not install curl with the app" FORCE)
if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "Use Windows native TLS" FORCE)
    set(CURL_USE_PKGCONFIG OFF CACHE BOOL "Avoid MinGW DLL dependencies discovered by pkg-config" FORCE)
    set(USE_WIN32_IDN ON CACHE BOOL "Use Windows IDN support instead of libidn2" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "Avoid a separate nghttp2 runtime DLL" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "Avoid a separate libpsl runtime DLL" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "Avoid an unused libssh2 runtime DLL" FORCE)
    set(ZLIB_USE_STATIC_LIBS ON CACHE BOOL "Link zlib statically into the release executables" FORCE)
elseif(APPLE)
    set(CURL_USE_SECTRANSP ON CACHE BOOL "Use macOS native TLS" FORCE)
    set(CURL_USE_PKGCONFIG OFF CACHE BOOL "Avoid Homebrew-only curl dependencies in universal builds" FORCE)
    set(CURL_DISABLE_LDAP ON CACHE BOOL "Disable unused LDAP protocols" FORCE)
    set(USE_APPLE_IDN ON CACHE BOOL "Use system IDN support" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "Avoid a non-system IDN dependency" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "Avoid a non-system HTTP/2 dependency" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "Avoid a non-system public suffix dependency" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "Avoid unused SSH dependencies" FORCE)
endif()

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

FetchContent_Declare(
    luau
    GIT_REPOSITORY https://github.com/luau-lang/luau.git
    GIT_TAG 0.743
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG v0.23.1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    curl
    URL https://curl.se/download/curl-8.10.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(raylib JoltPhysics enet nlohmann_json luau curl)

add_library(blubox_platform_http STATIC ../shared/HttpClient.cpp)
target_include_directories(blubox_platform_http PUBLIC ../shared)
target_link_libraries(blubox_platform_http PUBLIC CURL::libcurl)
if(APPLE)
    add_library(blubox_platform_native STATIC ../shared/NativeMac.mm)
    target_include_directories(blubox_platform_native PUBLIC ../shared)
    target_link_libraries(blubox_platform_native PUBLIC "-framework AppKit")
endif()

FetchContent_GetProperties(ufbx)
if(NOT ufbx_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
        FetchContent_Populate(ufbx)
        cmake_policy(POP)
    else()
        FetchContent_Populate(ufbx)
    endif()
endif()
add_library(ufbx STATIC "${ufbx_SOURCE_DIR}/ufbx.c")
target_include_directories(ufbx PUBLIC "${ufbx_SOURCE_DIR}")
set_target_properties(ufbx PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

add_executable(blubox_client WIN32
    src/ClientMain.cpp
    src/ClientSession.cpp
    src/NetworkClient.cpp
    src/PresenceReporter.cpp
    src/LaunchOptions.cpp
    src/ProtocolLaunchData.cpp
    src/PhysicsWorld.cpp
    src/Player.cpp
    src/PublishedWorld.cpp
    src/AvatarAssetCache.cpp
    src/LocalScriptRuntime.cpp
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

set(BLUBOX_RUNTIME_ICON_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxBrandIcon.hpp")
add_custom_command(
    OUTPUT "${BLUBOX_RUNTIME_ICON_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/assets/bloxity-icon.png"
            "-DOUTPUT_FILE=${BLUBOX_RUNTIME_ICON_HEADER}"
            "-DSYMBOL_NAME=kBrandIconData"
            "-DSIZE_SYMBOL_NAME=kBrandIconDataSize"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/bloxity-icon.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    VERBATIM
)
target_sources(blubox_client PRIVATE "${BLUBOX_RUNTIME_ICON_HEADER}")

set(BLUBOX_VERIFIED_BADGE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxVerifiedBadge.hpp")
add_custom_command(
    OUTPUT "${BLUBOX_VERIFIED_BADGE_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/verified-badge.png"
            "-DOUTPUT_FILE=${BLUBOX_VERIFIED_BADGE_HEADER}"
            "-DSYMBOL_NAME=kVerifiedBadgeData"
            "-DSIZE_SYMBOL_NAME=kVerifiedBadgeDataSize"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/verified-badge.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    VERBATIM
)
target_sources(blubox_client PRIVATE "${BLUBOX_VERIFIED_BADGE_HEADER}")

set(BLUBOX_CHAT_ICON_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxChatIcon.hpp")
add_custom_command(
    OUTPUT "${BLUBOX_CHAT_ICON_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/chat-comments.png"
            "-DOUTPUT_FILE=${BLUBOX_CHAT_ICON_HEADER}"
            "-DSYMBOL_NAME=kChatIconData"
            "-DSIZE_SYMBOL_NAME=kChatIconDataSize"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/chat-comments.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    VERBATIM
)
target_sources(blubox_client PRIVATE "${BLUBOX_CHAT_ICON_HEADER}")

# The Studio character geometry is intentionally kept as the visible model.
# It is animated procedurally because it has no skin or bone hierarchy.
set(BLUBOX_CHARACTER_MODEL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxR6Character.hpp")
add_custom_command(
    OUTPUT "${BLUBOX_CHARACTER_MODEL_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars/BluboxR6_Studio.glb"
            "-DOUTPUT_FILE=${BLUBOX_CHARACTER_MODEL_HEADER}"
            "-DSYMBOL_NAME=kR6CharacterData"
            "-DSIZE_SYMBOL_NAME=kR6CharacterDataSize"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars/BluboxR6_Studio.glb"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    VERBATIM
)
target_sources(blubox_client PRIVATE "${BLUBOX_CHARACTER_MODEL_HEADER}")

set(BLUBOX_WALK_ANIMATION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxR6WalkAnimation.hpp")
add_custom_command(
    OUTPUT "${BLUBOX_WALK_ANIMATION_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars/BluboxR6_Walk.glb"
            "-DOUTPUT_FILE=${BLUBOX_WALK_ANIMATION_HEADER}"
            "-DSYMBOL_NAME=kR6WalkAnimationData"
            "-DSIZE_SYMBOL_NAME=kR6WalkAnimationDataSize"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars/BluboxR6_Walk.glb"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    VERBATIM
)
target_sources(blubox_client PRIVATE "${BLUBOX_WALK_ANIMATION_HEADER}")

set(BLUBOX_JUMP_ANIMATION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxR6JumpAnimation.hpp")
add_custom_command(
    OUTPUT "${BLUBOX_JUMP_ANIMATION_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars/BluboxR6_Jump.glb"
            "-DOUTPUT_FILE=${BLUBOX_JUMP_ANIMATION_HEADER}"
            "-DSYMBOL_NAME=kR6JumpAnimationData"
            "-DSIZE_SYMBOL_NAME=kR6JumpAnimationDataSize"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars/BluboxR6_Jump.glb"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    VERBATIM
)
target_sources(blubox_client PRIVATE "${BLUBOX_JUMP_ANIMATION_HEADER}")

target_include_directories(blubox_client PRIVATE
    src
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    ${enet_SOURCE_DIR}/include
)
target_link_libraries(blubox_client PRIVATE raylib Jolt enet nlohmann_json::nlohmann_json Luau.Compiler Luau.VM ufbx blubox_platform_http)
if(APPLE)
    target_link_libraries(blubox_client PRIVATE blubox_platform_native)
endif()

if(MSVC)
    target_compile_options(blubox_client PRIVATE /W4 /permissive-)
else()
    target_compile_options(blubox_client PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32 AND MINGW)
    # Jolt propagates AVX2 flags to consumers. Raylib's by-value Matrix and
    # Camera operations can otherwise fault on MinGW stack alignment.
    set_source_files_properties(src/RaylibCompat.cpp src/AvatarAssetCache.cpp PROPERTIES
        COMPILE_OPTIONS "-mno-avx;-mno-avx2;-mno-fma;-mno-f16c"
    )
endif()

if(WIN32)
    target_compile_definitions(blubox_client PRIVATE _WIN32_WINNT=0x0600)
    target_link_libraries(blubox_client PRIVATE winmm winhttp)
    target_sources(blubox_client PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxIcon.rc")
endif()
if(WIN32 AND MINGW)
    # Release builds must run on Windows machines without a MinGW installation.
    target_link_options(blubox_client PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# The bootstrapper only links Raylib; it does not load Jolt or the game scene.
set(BLUBOX_LAUNCHER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/launcher-generated")
set(BLUBOX_LAUNCHER_FONT_HEADER "${BLUBOX_LAUNCHER_GENERATED_DIR}/BluboxLauncherFont.hpp")
set(BLUBOX_LAUNCHER_ICON_HEADER "${BLUBOX_LAUNCHER_GENERATED_DIR}/BluboxBrandIcon.hpp")
add_custom_command(
    OUTPUT "${BLUBOX_LAUNCHER_FONT_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BLUBOX_LAUNCHER_GENERATED_DIR}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Lato-Regular.ttf"
            "-DOUTPUT_FILE=${BLUBOX_LAUNCHER_FONT_HEADER}"
            "-DSYMBOL_NAME=kLauncherFontData"
            "-DSIZE_SYMBOL_NAME=kLauncherFontDataSize"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Lato-Regular.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    VERBATIM
)
add_custom_command(
    OUTPUT "${BLUBOX_LAUNCHER_ICON_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BLUBOX_LAUNCHER_GENERATED_DIR}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/assets/bloxity-icon.png"
            "-DOUTPUT_FILE=${BLUBOX_LAUNCHER_ICON_HEADER}"
            "-DSYMBOL_NAME=kBrandIconData"
            "-DSIZE_SYMBOL_NAME=kBrandIconDataSize"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/bloxity-icon.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
    VERBATIM
)

add_executable(blubox_launcher WIN32
    src/LauncherMain.cpp
    src/Launcher.cpp
    src/LaunchOptions.cpp
    src/ProtocolLaunchData.cpp
    src/WindowsProtocolRegistration.cpp
    "${BLUBOX_LAUNCHER_FONT_HEADER}"
    "${BLUBOX_LAUNCHER_ICON_HEADER}"
)
if(APPLE)
    set(BLUBOX_CLIENT_BUNDLE_EXECUTABLE "Blubox Client")
    configure_file(
        cmake/BluboxClient-Info.plist.in
        "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxClient-Info.plist"
        @ONLY
    )
    set_target_properties(blubox_launcher PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "${BLUBOX_CLIENT_BUNDLE_EXECUTABLE}"
        MACOSX_BUNDLE_BUNDLE_NAME "Blubox Client"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.blubox.client"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxClient-Info.plist"
    )
    add_custom_command(TARGET blubox_launcher POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:blubox_launcher>/Contents/Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.icns"
            "$<TARGET_BUNDLE_DIR:blubox_launcher>/Contents/Resources/AppIcon.icns"
        VERBATIM
    )
endif()
if(WIN32)
    target_sources(blubox_launcher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxIcon.rc")
endif()
target_include_directories(blubox_launcher PRIVATE src "${BLUBOX_LAUNCHER_GENERATED_DIR}")
target_link_libraries(blubox_launcher PRIVATE raylib nlohmann_json::nlohmann_json blubox_platform_http)
if(APPLE)
    target_link_libraries(blubox_launcher PRIVATE blubox_platform_native)
endif()
if(MSVC)
    target_compile_options(blubox_launcher PRIVATE /W4 /permissive-)
else()
    target_compile_options(blubox_launcher PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_compile_definitions(blubox_launcher PRIVATE _WIN32_WINNT=0x0600)
    target_link_libraries(blubox_launcher PRIVATE winmm advapi32 shell32 ole32 winhttp bcrypt)
endif()
if(WIN32 AND MINGW)
    target_link_options(blubox_launcher PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Dedicated updater entry point for the Studio Start Menu shortcut.
add_executable(blubox_studio_launcher WIN32
    src/LauncherMain.cpp
    src/Launcher.cpp
    src/LaunchOptions.cpp
    src/ProtocolLaunchData.cpp
    src/WindowsProtocolRegistration.cpp
    "${BLUBOX_LAUNCHER_FONT_HEADER}"
    "${BLUBOX_LAUNCHER_ICON_HEADER}"
)
if(WIN32)
    target_sources(blubox_studio_launcher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxIcon.rc")
endif()
target_include_directories(blubox_studio_launcher PRIVATE src "${BLUBOX_LAUNCHER_GENERATED_DIR}")
target_link_libraries(blubox_studio_launcher PRIVATE raylib nlohmann_json::nlohmann_json blubox_platform_http)
if(APPLE)
    target_link_libraries(blubox_studio_launcher PRIVATE blubox_platform_native)
endif()
target_compile_definitions(blubox_studio_launcher PRIVATE BLUBOX_STUDIO_LAUNCHER=1)
if(MSVC)
    target_compile_options(blubox_studio_launcher PRIVATE /W4 /permissive-)
else()
    target_compile_options(blubox_studio_launcher PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_compile_definitions(blubox_studio_launcher PRIVATE _WIN32_WINNT=0x0600)
    target_link_libraries(blubox_studio_launcher PRIVATE winmm advapi32 shell32 ole32 winhttp bcrypt)
endif()
if(WIN32 AND MINGW)
    target_link_options(blubox_studio_launcher PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Local mock release manifest and development payload for the bootstrapper.
configure_file(version.json version.json COPYONLY)
add_custom_command(TARGET blubox_launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/version.json $<TARGET_FILE_DIR:blubox_launcher>/version.json
    VERBATIM
)
add_custom_command(TARGET blubox_studio_launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/version.json $<TARGET_FILE_DIR:blubox_studio_launcher>/version.json
    VERBATIM
)
add_dependencies(blubox_launcher blubox_client)
