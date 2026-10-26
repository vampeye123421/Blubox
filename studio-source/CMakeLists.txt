cmake_minimum_required(VERSION 3.24)
project(BluboxStudio VERSION 0.1.11 LANGUAGES C CXX)
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(BLUBOX_STUDIO_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/bloxity-icon.ico")
set(BLUBOX_STUDIO_ICON_PATH "${BLUBOX_STUDIO_ICON}")
file(TO_CMAKE_PATH "${BLUBOX_STUDIO_ICON_PATH}" BLUBOX_STUDIO_ICON_PATH)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(cmake/BluboxStudioIcon.rc.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxStudioIcon.rc" @ONLY)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_GLTF OFF CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_PNG ON CACHE BOOL "" FORCE)
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
# Jolt's AVX2 flags propagate to the final Studio executable with MinGW. On
# Windows x64 this can produce aligned 256-bit stack spills in the large
# editor frame, although the ABI only guarantees 16-byte stack alignment.
# Studio's edit-time physics does not need AVX acceleration, so keep its Jolt
# build on the portable baseline instead of risking an access violation.
if(WIN32 AND MINGW)
  set(USE_AVX OFF CACHE BOOL "" FORCE)
  set(USE_AVX2 OFF CACHE BOOL "" FORCE)
  set(USE_AVX512 OFF CACHE BOOL "" FORCE)
  set(USE_F16C OFF CACHE BOOL "" FORCE)
  set(USE_FMADD OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.8-docking
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _blubox_source_only)
FetchContent_Declare(rlimgui
  GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
  GIT_TAG 1550009359ad975927f7f0e4a3f47e3f27123ea9
  SOURCE_SUBDIR _blubox_source_only)
FetchContent_Declare(imguizmo
  GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
  GIT_TAG 18cef5e031d8c6973d80284c67f60549fafd78c1
  SOURCE_SUBDIR _blubox_source_only)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE)
FetchContent_Declare(luau
  GIT_REPOSITORY https://github.com/luau-lang/luau.git
  GIT_TAG 0.743
  GIT_SHALLOW TRUE)
FetchContent_Declare(joltphysics
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG v5.3.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR Build)
FetchContent_Declare(enet
  GIT_REPOSITORY https://github.com/lsalzman/enet.git
  GIT_TAG v1.3.18
  GIT_SHALLOW TRUE)
FetchContent_Declare(tinyobjloader
  GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
  GIT_TAG v2.0.0rc13
  GIT_SHALLOW TRUE)
FetchContent_Declare(cgltf
  GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
  GIT_TAG v1.15
  GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(raylib nlohmann_json imgui rlimgui imguizmo luau joltphysics enet)
FetchContent_GetProperties(tinyobjloader)
if(NOT tinyobjloader_POPULATED)
  FetchContent_Populate(tinyobjloader)
endif()
FetchContent_GetProperties(cgltf)
if(NOT cgltf_POPULATED)
  FetchContent_Populate(cgltf)
endif()
target_include_directories(enet PUBLIC "${enet_SOURCE_DIR}/include")

add_library(blubox_studio_ui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${rlimgui_SOURCE_DIR}/rlImGui.cpp
  ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
target_include_directories(blubox_studio_ui SYSTEM PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/misc/cpp
  ${rlimgui_SOURCE_DIR}
  ${imguizmo_SOURCE_DIR}/src)
target_link_libraries(blubox_studio_ui PUBLIC raylib)

add_executable(blubox_studio WIN32
  studio/main.cpp
  studio/third_party/ImGuiColorTextEdit/TextEditor.cpp
  studio/AuthSession.cpp
  studio/Diagnostics.cpp
  studio/MapDocument.cpp
  studio/ScriptEngine.cpp
  studio/StudioBootstrap.cpp
  studio/WebsiteApi.cpp)
target_sources(blubox_studio PRIVATE
  engine/EngineCore.cpp
  engine/TaskScheduler.cpp
  engine/UiLayout.cpp
  engine/Remote.cpp
  engine/PhysicsWorld.cpp
  engine/LuaInstanceBinding.cpp)
target_sources(blubox_studio PRIVATE
  engine/EngineLoop.cpp engine/RaylibRenderAdapter.cpp engine/Scene.cpp
  engine/play/RuntimeSnapshot.cpp engine/play/PlayModeBridge.cpp engine/play/SpawnService.cpp
  engine/runtime/ServerRuntime.cpp engine/runtime/ClientRuntime.cpp
  engine/script/ScriptRuntime.cpp
  engine/ui/UiScene.cpp engine/ui/UiLayoutResolver.cpp engine/ui/backends/RaylibUiRenderer.cpp
  engine/network/EnetTransport.cpp engine/network/RemoteManager.cpp
  engine/network/RemoteRegistry.cpp engine/network/RemoteFunctionManager.cpp
  engine/network/ReplayGuard.cpp engine/network/RateLimiter.cpp)
target_sources(blubox_studio PRIVATE
  engine/replication/PropertyDeltaEncoder.cpp
  engine/replication/SnapshotBuffer.cpp
  engine/replication/InterpolationSystem.cpp
  engine/input/ClientInputSystem.cpp
  engine/runtime/HumanoidController.cpp
  engine/camera/GameCameraSystem.cpp
  engine/script/LuauSerializer.cpp
  engine/script/RemoteBinding.cpp)
target_sources(blubox_studio PRIVATE
  engine/editor/CompletionEngine.cpp
  engine/editor/CompletionController.cpp
  engine/intellisense/CompletionDatabase.cpp
  engine/intellisense/ContextAnalyzer.cpp
  engine/intellisense/FuzzyMatcher.cpp
  engine/editor/PropertiesPanel.cpp
  engine/assets/TextureCache.cpp
  engine/assets/ObjImporter.cpp
  engine/assets/GltfImporter.cpp
  engine/assets/MeshCache.cpp
  engine/render/RaylibMaterialAdapter.cpp
  engine/render/RaylibMeshAdapter.cpp
  engine/reflection/ReflectionRegistry.cpp)
set(BLUBOX_STUDIO_ICON_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxStudioIcon.hpp")
add_custom_command(
  OUTPUT "${BLUBOX_STUDIO_ICON_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND "${CMAKE_COMMAND}"
    "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/bloxity-icon.png"
    "-DOUTPUT_FILE=${BLUBOX_STUDIO_ICON_HEADER}"
    "-DSYMBOL_NAME=kBrandIconData"
    "-DSIZE_SYMBOL_NAME=kBrandIconDataSize"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/bloxity-icon.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
  VERBATIM)
target_sources(blubox_studio PRIVATE "${BLUBOX_STUDIO_ICON_HEADER}")
if(WIN32)
  target_sources(blubox_studio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/BluboxStudioIcon.rc")
endif()
target_include_directories(blubox_studio PRIVATE
  engine
  studio
  studio/third_party/ImGuiColorTextEdit
  "${tinyobjloader_SOURCE_DIR}"
  "${cgltf_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(blubox_studio PRIVATE
  BLUBOX_STUDIO_VERSION="${PROJECT_VERSION}"
  BLUBOX_STUDIO_MANIFEST_URL="https://raw.githubusercontent.com/vampeye123421/Blubox/main/studio-version.json")
target_link_libraries(blubox_studio PRIVATE
  blubox_studio_ui
  Luau.Compiler
  Luau.VM
  Jolt
  enet
  nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(blubox_studio PRIVATE winhttp crypt32 shell32 ole32 comdlg32 bcrypt)
endif()
if(WIN32 AND MINGW)
  target_link_options(blubox_studio PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if(MSVC)
  target_compile_options(blubox_studio PRIVATE /W4 /permissive-)
else()
  target_compile_options(blubox_studio PRIVATE -Wall -Wextra -Wno-stringop-overflow -Wno-sign-compare -Wno-unused-parameter -Wno-sequence-point -Wno-type-limits -Wno-unused-variable)
  add_compile_options(-Wno-stringop-overflow -Wno-sign-compare -Wno-unused-parameter -Wno-sequence-point -Wno-type-limits -Wno-unused-variable)
endif()

set(STUDIO_FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/editor/fonts")
add_custom_command(TARGET blubox_studio POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:blubox_studio>/assets/editor/fonts"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:blubox_studio>/assets/editor"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/editor/luau_api_catalog.json"
    "$<TARGET_FILE_DIR:blubox_studio>/assets/editor/luau_api_catalog.json"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STUDIO_FONT_DIR}/InterVariable.ttf"
    "$<TARGET_FILE_DIR:blubox_studio>/assets/editor/fonts/InterVariable.ttf"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STUDIO_FONT_DIR}/JetBrainsMono-Regular.ttf"
    "$<TARGET_FILE_DIR:blubox_studio>/assets/editor/fonts/JetBrainsMonoRegular.ttf"
  VERBATIM)

install(TARGETS blubox_studio RUNTIME DESTINATION .)
install(FILES
  assets/editor/luau_api_catalog.json
  DESTINATION assets/editor)
install(FILES
  "${STUDIO_FONT_DIR}/InterVariable.ttf"
  DESTINATION assets/editor/fonts)
install(FILES
  "${STUDIO_FONT_DIR}/JetBrainsMono-Regular.ttf"
  DESTINATION assets/editor/fonts
  RENAME JetBrainsMonoRegular.ttf)
