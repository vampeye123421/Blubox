#include "MapDocument.hpp"
#include "AuthSession.hpp"
#include "Diagnostics.hpp"
#include "ScriptEngine.hpp"
#include "StudioBootstrap.hpp"
#include "WebsiteApi.hpp"
#include "BluboxStudioIcon.hpp"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <rlImGui.h>
#include <ImGuizmo.h>
#include "TextEditor.h"
#include "../engine/play/PlayModeBridge.hpp"
#include "../engine/editor/CompletionEngine.hpp"
#include "../engine/editor/CompletionController.hpp"
#include "../engine/input/ClientInputSystem.hpp"
#include "../engine/input/InputRouter.hpp"
#include "../engine/camera/GameCameraSystem.hpp"
#include "../engine/ui/DarkTheme.hpp"
#include "../engine/reflection/ReflectionRegistry.hpp"
#include "../engine/editor/PropertiesPanel.hpp"
#include "../engine/assets/TextureCache.hpp"
#include "../engine/assets/MeshCache.hpp"
#include "../engine/assets/ObjImporter.hpp"
#include "../engine/assets/GltfImporter.hpp"
#include "../engine/assets/ImportedModel.hpp"
#include "../engine/render/RaylibMaterialAdapter.hpp"
#include "../engine/render/RaylibMeshAdapter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#include <commdlg.h>
#undef DrawText
#undef DrawTextEx
#undef ShowCursor
#undef CloseWindow
#undef Rectangle
#endif

namespace {

using blubox::studio::Entity;
using blubox::studio::MapDocument;
using blubox::studio::SessionStore;
using blubox::studio::WebsiteApi;

bool IsScriptObject(const Entity& entity);
bool IsGuiElement(const Entity& entity);
struct GuiElementRect { float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f; };
std::optional<GuiElementRect> GetGuiElementRect(
    const Entity& entity, const std::unordered_map<std::string, const Entity*>& byId,
    float viewportWidth, float viewportHeight, std::size_t depth = 0);

constexpr int kInitialWidth = 1440;
constexpr int kInitialHeight = 900;

ImFont* g_codeFont = nullptr;
std::vector<std::pair<float, ImFont*>> g_codeFontSizes;

ImFont* CodeFontForZoom(float zoom) {
    if (g_codeFontSizes.empty()) return g_codeFont;
    const float target = 13.0f * zoom;
    const auto closest = std::min_element(g_codeFontSizes.begin(), g_codeFontSizes.end(),
        [&](const auto& a, const auto& b) { return std::abs(a.first - target) < std::abs(b.first - target); });
    return closest->second;
}

ImVec4 Rgb(unsigned int color, float alpha = 1.0f) {
    return ImVec4(
        static_cast<float>((color >> 16) & 0xff) / 255.0f,
        static_cast<float>((color >> 8) & 0xff) / 255.0f,
        static_cast<float>(color & 0xff) / 255.0f,
        alpha);
}

ImVec4 ThemeColor(blubox::engine::ui::Color color, float alpha = 1.0f) {
    return ImVec4(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                  color.a / 255.0f * alpha);
}

void ApplyStudioTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    using blubox::engine::ui::kDarkTheme;
    const ImVec4 canvas = ThemeColor(kDarkTheme.canvas);
    const ImVec4 surface = ThemeColor(kDarkTheme.surface);
    const ImVec4 container = ThemeColor(kDarkTheme.container);
    const ImVec4 raised = ThemeColor(kDarkTheme.containerRaised);
    const ImVec4 overlay = ThemeColor(kDarkTheme.overlay);
    const ImVec4 border = ThemeColor(kDarkTheme.borderSubtle);
    const ImVec4 strongBorder = ThemeColor(kDarkTheme.borderStrong);
    const ImVec4 text = ThemeColor(kDarkTheme.textPrimary);
    const ImVec4 muted = ThemeColor(kDarkTheme.textMuted);
    const ImVec4 accent = ThemeColor(kDarkTheme.accent);
    const ImVec4 hover = ThemeColor(kDarkTheme.hover);
    const ImVec4 pressed = ThemeColor(kDarkTheme.pressed);

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = ThemeColor(kDarkTheme.textDisabled);
    colors[ImGuiCol_WindowBg] = surface;
    colors[ImGuiCol_ChildBg] = surface;
    colors[ImGuiCol_PopupBg] = overlay;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = Rgb(0x000000, 0.0f);
    colors[ImGuiCol_FrameBg] = raised;
    colors[ImGuiCol_FrameBgHovered] = hover;
    colors[ImGuiCol_FrameBgActive] = pressed;
    colors[ImGuiCol_TitleBg] = container;
    colors[ImGuiCol_TitleBgActive] = container;
    colors[ImGuiCol_TitleBgCollapsed] = surface;
    colors[ImGuiCol_MenuBarBg] = container;
    colors[ImGuiCol_ScrollbarBg] = surface;
    colors[ImGuiCol_ScrollbarGrab] = strongBorder;
    colors[ImGuiCol_ScrollbarGrabHovered] = muted;
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent;
    colors[ImGuiCol_Button] = raised;
    colors[ImGuiCol_ButtonHovered] = hover;
    colors[ImGuiCol_ButtonActive] = pressed;
    colors[ImGuiCol_Header] = ThemeColor(kDarkTheme.selected);
    colors[ImGuiCol_HeaderHovered] = hover;
    colors[ImGuiCol_HeaderActive] = pressed;
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_ResizeGrip] = ThemeColor(kDarkTheme.accent, 0.22f);
    colors[ImGuiCol_ResizeGripHovered] = ThemeColor(kDarkTheme.accent, 0.68f);
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_Tab] = surface;
    colors[ImGuiCol_TabHovered] = hover;
    colors[ImGuiCol_TabActive] = raised;
    colors[ImGuiCol_TabUnfocused] = surface;
    colors[ImGuiCol_TabUnfocusedActive] = container;
    colors[ImGuiCol_TabSelectedOverline] = accent;
    colors[ImGuiCol_DockingPreview] = ThemeColor(kDarkTheme.accent, 0.24f);
    colors[ImGuiCol_DockingEmptyBg] = canvas;
    colors[ImGuiCol_TableHeaderBg] = container;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = ThemeColor(kDarkTheme.borderSubtle, 0.65f);
    colors[ImGuiCol_TableRowBg] = ThemeColor(kDarkTheme.surface, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ThemeColor(kDarkTheme.textMuted, 0.035f);
    colors[ImGuiCol_TextSelectedBg] = ThemeColor(kDarkTheme.accent, 0.30f);
    colors[ImGuiCol_DragDropTarget] = accent;
    colors[ImGuiCol_NavHighlight] = accent;
    colors[ImGuiCol_NavWindowingHighlight] = Rgb(0xe2e8f0, 0.7f);
    colors[ImGuiCol_NavWindowingDimBg] = ThemeColor(kDarkTheme.canvas, 0.3f);
    colors[ImGuiCol_ModalWindowDimBg] = ThemeColor(kDarkTheme.canvas, 0.72f);

    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 4.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 9.0f;
    style.GrabMinSize = 12.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = kDarkTheme.radiusControl;
    style.PopupRounding = kDarkTheme.radiusMenu;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 3.0f;
    style.TabBarBorderSize = 1.0f;
    style.DockingSeparatorSize = 1.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
}

void LoadStudioFonts() {
    const std::filesystem::path fontDirectory =
        std::filesystem::path(GetApplicationDirectory()) / "assets" / "editor" / "fonts";
    const std::filesystem::path interfaceFont = fontDirectory / "InterVariable.ttf";
    const std::filesystem::path codeFont = fontDirectory / "JetBrainsMonoRegular.ttf";

    ImGuiIO& io = ImGui::GetIO();
    if (ImFont* font = io.Fonts->AddFontFromFileTTF(interfaceFont.string().c_str(), 14.0f)) {
        io.FontDefault = font;
    } else {
        TraceLog(LOG_WARNING, "Blubox Studio: Inter font not found at %s; using ImGui default.",
                 interfaceFont.string().c_str());
    }
    constexpr float sizes[] = {8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32};
    for (float size : sizes) {
        if (ImFont* font = io.Fonts->AddFontFromFileTTF(codeFont.string().c_str(), size)) {
            g_codeFontSizes.emplace_back(size, font);
            if (size == 13.0f) g_codeFont = font;
        }
    }
}

const TextEditor::Palette& LuauEditorPalette() {
    static const TextEditor::Palette palette = [] {
        TextEditor::Palette value = TextEditor::GetDarkPalette();
        const auto set = [&](TextEditor::PaletteIndex index, ImU32 color) {
            value[static_cast<std::size_t>(index)] = color;
        };
        set(TextEditor::PaletteIndex::Default, IM_COL32(220, 225, 234, 255));
        set(TextEditor::PaletteIndex::Keyword, IM_COL32(255, 121, 151, 255));
        set(TextEditor::PaletteIndex::Number, IM_COL32(255, 168, 112, 255));
        set(TextEditor::PaletteIndex::String, IM_COL32(158, 206, 106, 255));
        set(TextEditor::PaletteIndex::CharLiteral, IM_COL32(158, 206, 106, 255));
        set(TextEditor::PaletteIndex::Punctuation, IM_COL32(178, 187, 211, 255));
        set(TextEditor::PaletteIndex::Identifier, IM_COL32(220, 225, 234, 255));
        set(TextEditor::PaletteIndex::KnownIdentifier, IM_COL32(125, 207, 255, 255));
        set(TextEditor::PaletteIndex::Comment, IM_COL32(112, 122, 143, 255));
        set(TextEditor::PaletteIndex::MultiLineComment, IM_COL32(112, 122, 143, 255));
        set(TextEditor::PaletteIndex::Background, IM_COL32(15, 16, 20, 255));
        set(TextEditor::PaletteIndex::Cursor, IM_COL32(0, 210, 255, 255));
        set(TextEditor::PaletteIndex::Selection, IM_COL32(0, 134, 164, 120));
        set(TextEditor::PaletteIndex::LineNumber, IM_COL32(105, 112, 128, 255));
        set(TextEditor::PaletteIndex::CurrentLineFill, IM_COL32(34, 36, 43, 255));
        set(TextEditor::PaletteIndex::CurrentLineFillInactive, IM_COL32(29, 30, 36, 255));
        set(TextEditor::PaletteIndex::CurrentLineEdge, IM_COL32(49, 53, 64, 255));
        return value;
    }();
    return palette;
}

TextEditor::LanguageDefinition LuauLanguage() {
    TextEditor::LanguageDefinition language = TextEditor::LanguageDefinition::Lua();
    language.mName = "Luau";
    language.mKeywords.insert("continue");
    language.mKeywords.insert("export");
    language.mKeywords.insert("type");
    language.mKeywords.insert("typeof");
    for (const char* name : {
             "game", "workspace", "script", "task", "shared", "plugin",
             "Vector3", "Vector2", "CFrame", "Color3", "UDim2", "Instance",
             "Players", "Lighting", "Workspace", "ReplicatedStorage", "ServerStorage",
             "ServerScriptService", "StarterPlayer", "StarterGui", "RunService",
             "GetService", "WaitForChild", "FindFirstChild", "GetChildren", "Clone",
             "Destroy", "Connect", "Disconnect", "onTick", "onTouch", "onUntouch",
             "getPosition", "setPosition", "getRotation", "setRotation", "setColor",
             "setVisible", "applyForce", "applyImpulse", "broadcast", "spawnEntity",
             "destroyEntity", "findEntitiesInRadius", "getEntity", "getTime"}) {
        language.mIdentifiers.insert_or_assign(name,
            TextEditor::Identifier{TextEditor::Coordinates::Invalid(), "Blubox Luau API"});
    }
    return language;
}

struct SessionValidation {
    std::string token;
    std::string displayName;
    std::string error;
    bool valid = false;
};

struct StudioHandoffPoll {
    std::string token;
    std::string error;
};

struct OutputEntry {
    std::string level;
    std::string message;
};

struct ModelImportResult {
    blubox::engine::assets::ImportedModel model;
    blubox::engine::assets::ImportError error;
    bool succeeded = false;
};

struct StudioState {
    WebsiteApi api;
    MapDocument map = MapDocument::Create("My First World");
    std::unique_ptr<blubox::studio::ScriptEngine> scripts;
    std::unique_ptr<blubox::engine::play::PlayModeBridge> playMode;
    std::unique_ptr<blubox::engine::assets::TextureCache> textureCache;
    blubox::engine::assets::MeshCache meshCache;
    blubox::engine::render::RaylibMeshAdapter raylibMeshes;
    std::unordered_map<blubox::engine::assets::MaterialId, blubox::engine::assets::MaterialResource> materials;
    std::unordered_map<blubox::engine::assets::MaterialId, Material> raylibMaterials;
    std::unordered_map<std::string, Material> decalMaterials;
    Mesh decalQuad{};
    std::future<ModelImportResult> modelImportTask;
    RenderTexture2D viewport{};
    Camera3D camera{{15.0f, 12.0f, 15.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 55.0f, CAMERA_PERSPECTIVE};
    Camera3D editorCamera{};
    bool editorCameraSaved = false;
    std::optional<MapDocument> runtimeServerWorldCache;
    std::chrono::steady_clock::time_point nextServerViewRefresh{};
    blubox::engine::input::ClientInputSystem inputSystem;
    blubox::engine::input::InputRouter inputRouter;
    blubox::engine::camera::GameCameraComponent gameCamera;
    bool gameCursorCaptured = false;
    int selectedEntity = -1;
    std::string selectedService;
    int requestedWidth = 0;
    int requestedHeight = 0;
    ImGuizmo::OPERATION gizmoOperation = ImGuizmo::TRANSLATE;
    bool gizmoEnabled = true;
    std::string newMapName = "My World";
    std::string publishTitle;
    std::string publishDescription;
    std::string publishError;
    std::string lastPublishedSlug;
    std::string cloudWorldId;
    std::string websiteToken;
    std::string displayName;
    std::string authError;
    std::string status = "Ready";
    std::string lastReportedScriptError;
    std::vector<OutputEntry> output;
    std::vector<std::string> openScriptIds;
    std::unordered_map<std::string, std::unique_ptr<TextEditor>> scriptEditors;
    blubox::engine::editor::CompletionEngine completionEngine;
    blubox::engine::editor::CompletionController completionController;
    blubox::engine::editor::CompletionResponse completion;
    std::unordered_map<std::string, float> scriptZoom;
    std::unordered_map<std::string, std::string> scriptSavedSource;
    std::string completionForScript;
    int completionSelection = 0;
    int completionScroll = 0;
    std::chrono::steady_clock::time_point nextWorkspaceCompletionRefresh{};
    std::size_t workspaceCompletionHash = 0;
    std::unordered_set<std::string> dirtyScriptIds;
    std::string focusScriptId;
    std::string activeRibbonTab = "Home";
    ImGuiID centralDockNode = 0;
    std::filesystem::path currentFilePath;
    bool statusIsError = false;
    bool dirty = false;
    bool showExplorer = true;
    bool showProperties = true;
    bool showViewport = true;
    bool showOutput = true;
    bool showCloudPanel = false;
    bool showWorldPanel = false;
    bool pendingNewMap = false;
    bool pendingOpenMap = false;
    bool exitRequested = false;
    bool showPublishDialog = false;
    bool publishing = false;
    bool viewportHovered = false;
    bool viewportFocused = false;
    bool guiDragActive = false;
    int guiDragHandle = -2; // -1 moves the element; 0..7 resize handles.
    std::string guiDragEntityId;
    ImVec2 guiDragStartMouse{};
    std::array<int, 2> guiDragStartPosition{};
    std::array<int, 2> guiDragStartSize{};
    bool cameraLookCaptured = false;
    bool cameraAnglesInitialized = false;
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;
    bool authenticated = false;
    bool validatingSession = false;
    bool forceInteractiveLoginAfterValidation = false;
    bool loginBrowserOpened = false;
    bool projectOpen = false;
    std::future<SessionValidation> validationTask;
    std::future<StudioHandoffPoll> handoffPollTask;
    std::string pendingHandoffToken;
    std::string pendingHandoffId;
    std::string lastHandoffError;
    std::chrono::steady_clock::time_point nextHandoffPoll{};
    std::chrono::steady_clock::time_point handoffDeadline{};
    std::optional<std::filesystem::file_time_type> observedSessionFileTime;
    std::chrono::steady_clock::time_point nextSessionPoll{};
};

void AppendOutput(StudioState& state, std::string level, std::string message) {
    if (state.output.size() >= 500) state.output.erase(state.output.begin(), state.output.begin() + 100);
    state.output.push_back({std::move(level), std::move(message)});
}

ImVec4 StatusColor(bool error) {
    return error ? ImVec4(0.95f, 0.37f, 0.32f, 1.0f) : ImVec4(0.46f, 0.83f, 0.67f, 1.0f);
}

void SetStatus(StudioState& state, std::string message, bool error = false) {
    if (error) AppendOutput(state, "ERROR", message);
    state.status = std::move(message);
    state.statusIsError = error;
}

void AddEditorObject(StudioState& state, const std::string& kind, std::string parent) {
    try {
        Entity& object = state.map.AddEntity(kind);
        object.parent = parent;
        if (kind == "block" || kind == "sphere") {
            object.transform.position = {0.0f, 1.0f, 0.0f};
        } else if (kind == "spawnlocation") {
            object.transform.position = {0.0f, 0.5f, 0.0f};
            object.transform.scale = {4.0f, 1.0f, 4.0f};
            object.color = {62, 157, 111, 255};
            object.components = nlohmann::json::array({{{"type", "SpawnLocation"}}});
            object.properties = {{"Enabled", true}, {"Duration", 0.0}, {"Neutral", true},
                {"TeamColor", {194, 194, 194, 255}}, {"AllowTeamChangeOnTouch", false}, {"Anchored", true}};
        } else if (kind == "decal") {
            object.properties = {{"textureId", std::uint64_t{0}}, {"size", {2.0, 2.0}},
                {"uvOffset", {0.0, 0.0}}, {"uvScale", {1.0, 1.0}}, {"rotationDegrees", 0.0},
                {"transparency", 0.0}, {"normalBlend", 0.0}, {"tint", {255, 255, 255, 255}}, {"depthTest", true}};
            object.components = nlohmann::json::array({{{"type", "Decal"}}});
        } else if (kind == "script" || kind == "localscript") {
            object.luauScript = "-- This script belongs to its parent object.\n"
                                "-- Run it, then touch the part with another part.\n"
                                "body.onTouch(function(other)\n"
                                "    body.setColor(255, 92, 92, 255)\n"
                                "    print(other and other.name or \"Touched\")\n"
                                "end)\n";
        }
        state.selectedEntity = static_cast<int>(state.map.entities.size() - 1);
        state.selectedService.clear();
        state.showExplorer = true;
        state.dirty = true;
        if (kind == "script" || kind == "localscript") {
            state.openScriptIds.push_back(object.id);
            state.focusScriptId = object.id;
        }
        AppendOutput(state, "INFO", "Created " + object.name + " in " + parent + ".");
        SetStatus(state, "Created " + object.name + ".");
    } catch (const std::exception& exception) {
        SetStatus(state, exception.what(), true);
    }
}

std::string ParentForNewScript(const StudioState& state) {
    if (state.selectedEntity >= 0 && state.selectedEntity < static_cast<int>(state.map.entities.size())) {
        const Entity& selected = state.map.entities[static_cast<std::size_t>(state.selectedEntity)];
        if (!IsScriptObject(selected) && selected.kind != "screengui" && selected.kind != "frame" &&
            selected.kind != "textlabel" && selected.kind != "textbutton") return selected.id;
        if (selected.kind == "screengui" || selected.kind == "frame") return selected.id;
    }
    if (!state.selectedService.empty()) return state.selectedService;
    return "Workspace";
}

void AddScriptForSelection(StudioState& state, bool localScript) {
    std::string parent = ParentForNewScript(state);
    if (localScript && parent == "Workspace" && state.selectedEntity < 0 && state.selectedService.empty())
        parent = "StarterPlayer";
    AddEditorObject(state, localScript ? "localscript" : "script", std::move(parent));
}

void CreateScriptEngine(StudioState& state) {
    state.scripts = std::make_unique<blubox::studio::ScriptEngine>(state.map);
    state.scripts->SetBroadcastHandler([&state](std::string message) {
        SetStatus(state, "Script broadcast: " + message);
    });
    state.scripts->SetOutputHandler([&state](std::string message, bool warning) {
        AppendOutput(state, warning ? "WARNING" : "INFO", std::move(message));
    });
}

std::string PathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::optional<std::filesystem::path> ChooseMapPath(bool save) {
#ifdef _WIN32
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrFilter = L"Blubox map (*.blubox)\0*.blubox\0JSON map (*.json)\0*.json\0All files (*.*)\0*.*\0\0";
    dialog.lpstrDefExt = L"blubox";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL selected = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!selected) return std::nullopt;
    std::filesystem::path result(path.data());
    if (save && result.extension().empty()) result += L".blubox";
    return result;
#else
    (void)save;
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path> ChooseModelPath() {
#ifdef _WIN32
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrFilter = L"3D models (*.obj;*.gltf;*.glb)\0*.obj;*.gltf;*.glb\0OBJ (*.obj)\0*.obj\0glTF / GLB (*.gltf;*.glb)\0*.gltf;*.glb\0\0";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::optional<std::filesystem::path>(path.data()) : std::nullopt;
#else
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path> ChooseTexturePath() {
#ifdef _WIN32
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr;*.dds)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr;*.dds\0All files (*.*)\0*.*\0\0";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::optional<std::filesystem::path>(path.data()) : std::nullopt;
#else
    return std::nullopt;
#endif
}

void QueueModelImport(StudioState& state) {
    if (state.modelImportTask.valid() || !state.textureCache) {
        SetStatus(state, "A model import is already running or asset services are not initialized.", true);
        return;
    }
    auto selected = ChooseModelPath();
    if (!selected) return;
    const auto path = *selected;
    auto* textures = state.textureCache.get();
    state.modelImportTask = std::async(std::launch::async, [path, textures] {
        ModelImportResult result;
        const auto extension = path.extension().string();
        if (extension == ".obj" || extension == ".OBJ") {
            blubox::engine::assets::ObjImporter importer(textures);
            result.succeeded = importer.import(path, result.model, result.error);
        } else {
            blubox::engine::assets::GltfImporter importer(textures);
            result.succeeded = importer.import(path, result.model, result.error);
        }
        return result;
    });
    SetStatus(state, "Importing model in the background…");
}

std::uint64_t StableMaterialId(const std::filesystem::path& source, std::uint64_t sourceId) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : source.generic_string()) { hash ^= byte; hash *= 1099511628211ull; }
    for (unsigned i = 0; i < 8; ++i) { hash ^= static_cast<std::uint8_t>(sourceId >> (i * 8)); hash *= 1099511628211ull; }
    return hash ? hash : 1;
}

void DestroyRaylibMaterial(Material& material) {
    // Raylib's UnloadMaterial frees every non-default texture referenced by its
    // map array. Here those textures are shared and owned by TextureCache.
    if (material.maps) {
        for (int i = 0; i <= MATERIAL_MAP_BRDF; ++i)
            material.maps[i].texture.id = rlGetTextureIdDefault();
    }
    UnloadMaterial(material);
    material = {};
}

void ChooseAlbedoForEntity(StudioState& state, Entity& entity) {
    if (!state.textureCache) return;
    auto path = ChooseTexturePath();
    if (!path) return;
    std::string error;
    const auto texture = state.textureCache->acquire(*path, &error);
    if (!texture) { SetStatus(state, error, true); return; }
    auto materialId = entity.properties.value("materialId", std::uint64_t{0});
    if (materialId == 0) materialId = StableMaterialId(*path, StableMaterialId(std::filesystem::path(entity.id), texture));
    auto& material = state.materials[materialId];
    material.id = materialId;
    material.name = entity.name + " Material";
    material.properties.baseColor = entity.color;
    material.properties.roughness = entity.properties.value("roughness", 0.8f);
    material.properties.metalness = entity.properties.value("metalness", 0.0f);
    material.albedoMap = {texture, blubox::engine::assets::TextureType::Albedo};
    entity.properties["materialId"] = materialId;
    entity.properties["albedoMap"] = texture;
    auto gpu = state.raylibMaterials.find(materialId);
    if (gpu == state.raylibMaterials.end()) gpu = state.raylibMaterials.emplace(materialId, LoadMaterialDefault()).first;
    blubox::engine::render::RaylibMaterialAdapter adapter(*state.textureCache);
    adapter.apply(gpu->second, material);
    state.dirty = true;
    SetStatus(state, "Albedo texture queued for upload. It will appear when the render-thread upload is ready.");
}

void ChooseTextureForDecal(StudioState& state, Entity& entity) {
    if (!state.textureCache) return;
    const auto path = ChooseTexturePath();
    if (!path) return;
    std::string error;
    const auto texture = state.textureCache->acquire(*path, &error);
    if (!texture) { SetStatus(state, error, true); return; }
    entity.properties["textureId"] = texture;
    state.dirty = true;
    SetStatus(state, "Decal texture is loading.");
}

void PumpModelImport(StudioState& state) {
    if (!state.modelImportTask.valid() || state.modelImportTask.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
    ModelImportResult result;
    try { result = state.modelImportTask.get(); }
    catch (const std::exception& exception) { SetStatus(state, std::string("Model import failed: ") + exception.what(), true); return; }
    if (!result.succeeded) { SetStatus(state, result.error.message.empty() ? "Model import failed." : result.error.message, true); return; }

    std::vector<std::string> createdEntities;
    std::vector<blubox::engine::assets::MeshAssetId> createdMeshes;
    std::vector<blubox::engine::assets::MaterialId> createdMaterials;
    const auto rollback = [&] {
        for (auto id : createdMeshes) { state.raylibMeshes.release(id); state.meshCache.release(id); }
        for (auto id : createdMaterials) {
            auto material = state.raylibMaterials.find(id);
            if (material != state.raylibMaterials.end()) { DestroyRaylibMaterial(material->second); state.raylibMaterials.erase(material); }
            state.materials.erase(id);
        }
        std::erase_if(state.map.entities, [&](const Entity& entity) {
            return std::find(createdEntities.begin(), createdEntities.end(), entity.id) != createdEntities.end();
        });
    };

    try {
        auto& root = state.map.AddEntity("model");
        root.name = result.model.sourcePath.stem().string();
        root.parent = "Workspace";
        const std::string rootId = root.id;
        createdEntities.push_back(rootId);

        std::unordered_map<blubox::engine::assets::MaterialId, blubox::engine::assets::MaterialId> materialRemap;
        for (auto& material : result.model.materials) {
            const auto sourceId = material.id;
            material.id = StableMaterialId(result.model.sourcePath, sourceId);
            materialRemap.emplace(sourceId, material.id);
            state.materials[material.id] = material;
            Material gpuMaterial = LoadMaterialDefault();
            blubox::engine::render::RaylibMaterialAdapter adapter(*state.textureCache);
            adapter.apply(gpuMaterial, material);
            state.raylibMaterials.emplace(material.id, gpuMaterial);
            createdMaterials.push_back(material.id);
        }

        std::vector<std::string> nodeIds(result.model.nodes.size());
        // Create all node instances before assigning parents: glTF node order is
        // not required to be topological, so a parent may appear later.
        for (std::size_t i = 0; i < result.model.nodes.size(); ++i) {
            const auto& node = result.model.nodes[i];
            auto& entity = state.map.AddEntity("model");
            entity.name = node.name.empty() ? "Node " + std::to_string(i + 1) : node.name;
            entity.parent = rootId;
            entity.transform.position = {node.position.x, node.position.y, node.position.z};
            entity.transform.scale = {node.scale.x, node.scale.y, node.scale.z};
            entity.transform.rotation = {
                std::atan2(2.0f * (node.rotation[3] * node.rotation[0] + node.rotation[1] * node.rotation[2]), 1.0f - 2.0f * (node.rotation[0] * node.rotation[0] + node.rotation[1] * node.rotation[1])),
                std::asin(std::clamp(2.0f * (node.rotation[3] * node.rotation[1] - node.rotation[2] * node.rotation[0]), -1.0f, 1.0f)),
                std::atan2(2.0f * (node.rotation[3] * node.rotation[2] + node.rotation[0] * node.rotation[1]), 1.0f - 2.0f * (node.rotation[1] * node.rotation[1] + node.rotation[2] * node.rotation[2]))};
            nodeIds[i] = entity.id;
            createdEntities.push_back(entity.id);
        }
        for (std::size_t i = 0; i < result.model.nodes.size(); ++i) {
            const auto parent = result.model.nodes[i].parentIndex;
            if (parent != UINT32_MAX) {
                if (parent >= nodeIds.size() || parent == i) throw std::runtime_error("Imported model has an invalid node parent.");
                const auto node = std::find_if(state.map.entities.begin(), state.map.entities.end(), [&](const Entity& e) { return e.id == nodeIds[i]; });
                if (node == state.map.entities.end()) throw std::runtime_error("Imported node instance was lost during hierarchy creation.");
                node->parent = nodeIds[parent];
            }
        }

        for (std::size_t nodeIndex = 0; nodeIndex < result.model.nodes.size(); ++nodeIndex) {
            for (auto meshIndex : result.model.nodes[nodeIndex].meshIndices) {
                auto mesh = std::move(result.model.meshes.at(meshIndex));
                const auto materialSourceId = mesh.materialId;
                if (materialRemap.contains(materialSourceId)) mesh.materialId = materialRemap.at(materialSourceId);
                const auto assetId = state.meshCache.insert(mesh);
                if (!assetId) throw std::runtime_error("Could not cache an imported mesh.");
                createdMeshes.push_back(assetId);
                std::string meshError;
                if (!state.raylibMeshes.upload(assetId, mesh, &meshError)) throw std::runtime_error(meshError);
                auto& part = state.map.AddEntity("meshpart");
                part.name = mesh.name.empty() ? "MeshPart" : mesh.name;
                part.parent = nodeIds[nodeIndex];
                part.properties["meshAssetId"] = assetId;
                part.properties["materialId"] = mesh.materialId;
                part.components.push_back({{"type", "MeshPart"}, {"meshAssetId", assetId}, {"materialId", mesh.materialId}});
                const auto material = state.materials.find(mesh.materialId);
                if (material != state.materials.end()) {
                    const auto& p = material->second.properties;
                    part.color = {p.baseColor[0], p.baseColor[1], p.baseColor[2], p.baseColor[3]};
                    part.properties["roughness"] = p.roughness;
                    part.properties["metalness"] = p.metalness;
                    part.properties["emissiveIntensity"] = p.emissiveIntensity;
                    part.properties["albedoMap"] = material->second.albedoMap.assetId;
                    part.properties["normalMap"] = material->second.normalMap.assetId;
                    part.properties["roughnessMap"] = material->second.roughnessMap.assetId;
                    part.properties["metalnessMap"] = material->second.metalnessMap.assetId;
                    part.properties["emissiveMap"] = material->second.emissiveMap.assetId;
                }
                createdEntities.push_back(part.id);
            }
        }
    } catch (const std::exception& exception) {
        rollback();
        SetStatus(state, std::string("Model import could not be added: ") + exception.what(), true);
        return;
    }
    state.selectedEntity = static_cast<int>(state.map.entities.size() - 1);
    state.selectedService.clear();
    state.dirty = true;
    SetStatus(state, "Imported " + result.model.sourcePath.filename().string() + " with " + std::to_string(createdMeshes.size()) + " mesh part(s).");
}

void ReleaseProjectAssets(StudioState& state) {
    for (auto& [id, material] : state.raylibMaterials) { (void)id; DestroyRaylibMaterial(material); }
    state.raylibMaterials.clear();
    for (auto& [id, material] : state.decalMaterials) { (void)id; DestroyRaylibMaterial(material); }
    state.decalMaterials.clear();
    state.materials.clear();
    state.raylibMeshes.clear();
    state.meshCache.clear();
    if (state.textureCache) state.textureCache->clear();
}

void RefreshRaylibMaterials(StudioState& state) {
    if (!state.textureCache) return;
    blubox::engine::render::RaylibMaterialAdapter adapter(*state.textureCache);
    for (const auto& entity : state.map.entities) {
        const auto id = entity.properties.value("materialId", std::uint64_t{0});
        const auto found = state.materials.find(id);
        if (!id || found == state.materials.end()) continue;
        auto& source = found->second;
        source.properties.baseColor = entity.color;
        source.properties.roughness = std::clamp(entity.properties.value("roughness", source.properties.roughness), 0.0f, 1.0f);
        source.properties.metalness = std::clamp(entity.properties.value("metalness", source.properties.metalness), 0.0f, 1.0f);
        source.properties.emissiveIntensity = std::clamp(entity.properties.value("emissiveIntensity", source.properties.emissiveIntensity), 0.0f, 1000.0f);
        const auto setMap = [&](const char* key, blubox::engine::assets::TextureReference& reference) {
            auto texture = reference.assetId;
            const auto value = entity.properties.find(key);
            if (value != entity.properties.end() && (value->is_number_unsigned() || value->is_number_integer())) {
                try { texture = value->get<std::uint64_t>(); } catch (...) { return; }
            }
            if (texture == 0) reference.assetId = 0;
            else reference.assetId = texture;
        };
        setMap("albedoMap", source.albedoMap);
        setMap("normalMap", source.normalMap);
        setMap("roughnessMap", source.roughnessMap);
        setMap("metalnessMap", source.metalnessMap);
        setMap("emissiveMap", source.emissiveMap);
        if (entity.properties.contains("uvScale") && entity.properties["uvScale"].is_array() && entity.properties["uvScale"].size() == 2 &&
            entity.properties["uvScale"][0].is_number() && entity.properties["uvScale"][1].is_number())
            source.properties.uvTiling = {entity.properties["uvScale"][0].get<float>(), entity.properties["uvScale"][1].get<float>()};
        if (entity.properties.contains("uvOffset") && entity.properties["uvOffset"].is_array() && entity.properties["uvOffset"].size() == 2 &&
            entity.properties["uvOffset"][0].is_number() && entity.properties["uvOffset"][1].is_number())
            source.properties.uvOffset = {entity.properties["uvOffset"][0].get<float>(), entity.properties["uvOffset"][1].get<float>()};
        source.properties.uvRotationDegrees = entity.properties.value("uvRotationDegrees", source.properties.uvRotationDegrees);
    }
    for (auto& [id, material] : state.raylibMaterials) {
        const auto source = state.materials.find(id);
        if (source != state.materials.end()) adapter.apply(material, source->second);
    }
    std::unordered_set<std::string> liveDecals;
    for (const auto& entity : state.map.entities) {
        if (entity.kind != "decal") continue;
        const auto textureId = entity.properties.value("textureId", std::uint64_t{0});
        if (!textureId) continue;
        liveDecals.insert(entity.id);
        auto found = state.decalMaterials.find(entity.id);
        if (found == state.decalMaterials.end()) found = state.decalMaterials.emplace(entity.id, LoadMaterialDefault()).first;
        blubox::engine::assets::MaterialResource resource;
        resource.id = textureId;
        resource.name = entity.name;
        const auto tint = entity.properties.value("tint", nlohmann::json::array({255,255,255,255}));
        if (tint.is_array() && tint.size() == 4) {
            const auto transparency = std::clamp(entity.properties.value("transparency", 0.0f), 0.0f, 1.0f);
            resource.properties.baseColor = {
                static_cast<std::uint8_t>(std::clamp(tint[0].get<int>(), 0, 255)),
                static_cast<std::uint8_t>(std::clamp(tint[1].get<int>(), 0, 255)),
                static_cast<std::uint8_t>(std::clamp(tint[2].get<int>(), 0, 255)),
                static_cast<std::uint8_t>(std::clamp(static_cast<int>(tint[3].get<int>() * (1.0f - transparency)), 0, 255))};
        }
        resource.albedoMap = {textureId, blubox::engine::assets::TextureType::Decal};
        adapter.apply(found->second, resource);
    }
    std::erase_if(state.decalMaterials, [&](auto& entry) {
        if (liveDecals.contains(entry.first)) return false;
        DestroyRaylibMaterial(entry.second);
        return true;
    });
}

bool CreateDecalQuad(Mesh& mesh) {
    mesh = {};
    mesh.vertexCount = 4;
    mesh.triangleCount = 2;
    mesh.vertices = static_cast<float*>(MemAlloc(12 * sizeof(float)));
    mesh.normals = static_cast<float*>(MemAlloc(12 * sizeof(float)));
    mesh.texcoords = static_cast<float*>(MemAlloc(8 * sizeof(float)));
    mesh.indices = static_cast<unsigned short*>(MemAlloc(6 * sizeof(unsigned short)));
    if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.indices) { UnloadMesh(mesh); mesh = {}; return false; }
    const float vertices[]{-0.5f,0,-0.5f, -0.5f,0,0.5f, 0.5f,0,0.5f, 0.5f,0,-0.5f};
    const float normals[]{0,1,0, 0,1,0, 0,1,0, 0,1,0};
    const float uvs[]{0,0, 0,1, 1,1, 1,0};
    const unsigned short indices[]{0,1,2, 0,2,3};
    std::copy(std::begin(vertices), std::end(vertices), mesh.vertices);
    std::copy(std::begin(normals), std::end(normals), mesh.normals);
    std::copy(std::begin(uvs), std::end(uvs), mesh.texcoords);
    std::copy(std::begin(indices), std::end(indices), mesh.indices);
    UploadMesh(&mesh, false);
    return mesh.vaoId != 0 || mesh.vboId != nullptr;
}

Matrix WorldMatrix(const MapDocument& world, const Entity& entity) {
    std::vector<const Entity*> chain;
    const Entity* current = &entity;
    for (std::size_t depth = 0; current && depth <= world.entities.size(); ++depth) {
        chain.push_back(current);
        if (current->parent == "Workspace") break;
        const auto parent = std::find_if(world.entities.begin(), world.entities.end(), [&](const Entity& candidate) { return candidate.id == current->parent; });
        if (parent == world.entities.end()) return MatrixIdentity();
        current = &*parent;
    }
    Matrix transform = MatrixIdentity();
    for (const Entity* item : chain) {
        const auto& t = item->transform;
        const Matrix local = MatrixMultiply(MatrixMultiply(MatrixScale(t.scale[0], t.scale[1], t.scale[2]),
            MatrixRotateXYZ(Vector3{t.rotation[0], t.rotation[1], t.rotation[2]})),
            MatrixTranslate(t.position[0], t.position[1], t.position[2]));
        transform = MatrixMultiply(transform, local);
    }
    return transform;
}

bool IsWorkspaceDescendant(const MapDocument& world, const Entity& entity) {
    const Entity* current = &entity;
    for (std::size_t depth = 0; current && depth <= world.entities.size(); ++depth) {
        if (current->parent == "Workspace") return true;
        const auto parent = std::find_if(world.entities.begin(), world.entities.end(),
            [&](const Entity& candidate) { return candidate.id == current->parent; });
        if (parent == world.entities.end()) return false;
        current = &*parent;
    }
    return false;
}

bool IsScriptObject(const Entity& entity);

std::filesystem::path MapAssetDirectory(const std::filesystem::path& mapPath) {
    return std::filesystem::path(mapPath.string() + ".assets");
}

nlohmann::json MaterialToJson(const blubox::engine::assets::MaterialResource& material) {
    const auto ref = [](const blubox::engine::assets::TextureReference& value) {
        return nlohmann::json{{"assetId", value.assetId}, {"type", static_cast<unsigned>(value.type)}};
    };
    const auto& p = material.properties;
    return {{"id", material.id}, {"name", material.name},
        {"albedo", ref(material.albedoMap)}, {"normal", ref(material.normalMap)},
        {"roughnessMap", ref(material.roughnessMap)}, {"metalnessMap", ref(material.metalnessMap)},
        {"emissive", ref(material.emissiveMap)}, {"ambientOcclusion", ref(material.ambientOcclusionMap)},
        {"baseColor", p.baseColor}, {"roughness", p.roughness}, {"metalness", p.metalness},
        {"emissiveIntensity", p.emissiveIntensity}, {"normalScale", p.normalScale},
        {"uvTiling", {p.uvTiling.x, p.uvTiling.y}}, {"uvOffset", {p.uvOffset.x, p.uvOffset.y}},
        {"uvRotationDegrees", p.uvRotationDegrees}};
}

blubox::engine::assets::MaterialResource MaterialFromJson(const nlohmann::json& value) {
    using namespace blubox::engine::assets;
    if (!value.is_object()) throw std::runtime_error("Map material entry is invalid.");
    MaterialResource material;
    material.id = value.at("id").get<MaterialId>();
    material.name = value.value("name", std::string("Material"));
    const auto readRef = [&](const char* key, TextureType type) {
        TextureReference ref{0, type};
        if (value.contains(key) && value.at(key).is_object()) ref.assetId = value.at(key).value("assetId", AssetId{0});
        return ref;
    };
    material.albedoMap = readRef("albedo", TextureType::Albedo);
    material.normalMap = readRef("normal", TextureType::Normal);
    material.roughnessMap = readRef("roughnessMap", TextureType::Roughness);
    material.metalnessMap = readRef("metalnessMap", TextureType::Metalness);
    material.emissiveMap = readRef("emissive", TextureType::Emissive);
    material.ambientOcclusionMap = readRef("ambientOcclusion", TextureType::AmbientOcclusion);
    auto& p = material.properties;
    if (value.contains("baseColor") && value["baseColor"].is_array() && value["baseColor"].size() == 4)
        p.baseColor = value["baseColor"].get<std::array<std::uint8_t, 4>>();
    p.roughness = std::clamp(value.value("roughness", 0.8f), 0.0f, 1.0f);
    p.metalness = std::clamp(value.value("metalness", 0.0f), 0.0f, 1.0f);
    p.emissiveIntensity = std::clamp(value.value("emissiveIntensity", 0.0f), 0.0f, 1000.0f);
    p.normalScale = std::clamp(value.value("normalScale", 1.0f), 0.0f, 100.0f);
    if (value.contains("uvTiling") && value["uvTiling"].is_array() && value["uvTiling"].size() == 2) {
        p.uvTiling = {value["uvTiling"][0].get<float>(), value["uvTiling"][1].get<float>()};
    }
    if (value.contains("uvOffset") && value["uvOffset"].is_array() && value["uvOffset"].size() == 2)
        p.uvOffset = {value["uvOffset"][0].get<float>(), value["uvOffset"][1].get<float>()};
    p.uvRotationDegrees = value.value("uvRotationDegrees", 0.0f);
    return material;
}

bool SaveMapAssets(const StudioState& state, const std::filesystem::path& mapPath, std::string& error) {
    using namespace blubox::engine::assets;
    const auto directory = MapAssetDirectory(mapPath);
    std::error_code ec;
    std::filesystem::create_directories(directory / "meshes", ec);
    if (ec) { error = "Could not create the map asset folder: " + ec.message(); return false; }
    nlohmann::json manifest{{"version", 1}, {"materials", nlohmann::json::array()},
                            {"textures", nlohmann::json::object()}};
    std::unordered_set<MeshAssetId> meshIds;
    std::unordered_set<MaterialId> materialIds;
    for (const auto& entity : state.map.entities) {
        if (entity.kind == "meshpart") {
            const auto meshId = entity.properties.value("meshAssetId", std::uint64_t{0});
            if (meshId) meshIds.insert(meshId);
        }
        const auto materialId = entity.properties.value("materialId", std::uint64_t{0});
        if (materialId) materialIds.insert(materialId);
    }
    for (const auto id : meshIds) {
        const auto file = directory / "meshes" / (std::to_string(id) + ".mesh");
        if (!state.meshCache.saveAsset(id, file, error)) return false;
    }
    std::unordered_map<AssetId, std::string> textureFiles;
    const auto recordTexture = [&](const TextureReference& reference) -> bool {
        if (!reference.assetId || textureFiles.contains(reference.assetId)) return true;
        if (!state.textureCache) { error = "Texture cache is unavailable while saving this map."; return false; }
        const auto source = state.textureCache->sourcePath(reference.assetId);
        if (source.empty() || !std::filesystem::is_regular_file(source, ec) || ec) {
            error = "A referenced texture source file is unavailable; map assets were not saved."; return false;
        }
        const auto relative = std::filesystem::path("textures") /
            (std::to_string(reference.assetId) + source.extension().string());
        std::filesystem::create_directories((directory / relative).parent_path(), ec);
        if (ec) { error = "Could not create the texture asset folder."; return false; }
        std::filesystem::copy_file(source, directory / relative, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) { error = "Could not copy a texture into the map asset folder: " + ec.message(); return false; }
        textureFiles.emplace(reference.assetId, relative.generic_string());
        manifest["textures"][std::to_string(reference.assetId)] = relative.generic_string();
        return true;
    };
    for (const auto id : materialIds) {
        const auto found = state.materials.find(id);
        if (found == state.materials.end()) continue;
        const auto& m = found->second;
        if (!recordTexture(m.albedoMap) || !recordTexture(m.normalMap) || !recordTexture(m.roughnessMap) ||
            !recordTexture(m.metalnessMap) || !recordTexture(m.emissiveMap) || !recordTexture(m.ambientOcclusionMap)) return false;
        manifest["materials"].push_back(MaterialToJson(m));
    }
    for (const auto& entity : state.map.entities) {
        if (entity.kind != "decal") continue;
        const auto id = entity.properties.value("textureId", std::uint64_t{0});
        if (id && !recordTexture(TextureReference{id, TextureType::Decal})) return false;
    }
    std::ofstream out(directory / "manifest.json", std::ios::binary | std::ios::trunc);
    if (!out) { error = "Could not open the map asset manifest."; return false; }
    out << manifest.dump(2) << '\n';
    if (!out) { error = "Could not write the map asset manifest."; return false; }
    error.clear();
    return true;
}

void LoadMapAssets(StudioState& state, const std::filesystem::path& mapPath) {
    using namespace blubox::engine::assets;
    const auto directory = MapAssetDirectory(mapPath);
    const auto manifestPath = directory / "manifest.json";
    std::error_code ec;
    if (!std::filesystem::exists(manifestPath, ec)) {
        const bool hasMesh = std::any_of(state.map.entities.begin(), state.map.entities.end(), [](const Entity& e) {
            return (e.kind == "meshpart" && e.properties.value("meshAssetId", std::uint64_t{0}) != 0) ||
                   (e.kind == "decal" && e.properties.value("textureId", std::uint64_t{0}) != 0) ||
                   e.properties.value("albedoMap", std::uint64_t{0}) != 0;
        });
        if (hasMesh) throw std::runtime_error("This map references imported meshes, but its .assets sidecar folder is missing.");
        return;
    }
    std::ifstream input(manifestPath, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open the map asset manifest.");
    nlohmann::json manifest; input >> manifest;
    if (!manifest.is_object() || manifest.value("version", 0) != 1 || !manifest.value("materials", nlohmann::json::array()).is_array() ||
        !manifest.value("textures", nlohmann::json::object()).is_object()) throw std::runtime_error("Map asset manifest version or structure is invalid.");
    state.textureCache->clear();
    std::unordered_map<AssetId, AssetId> textureRemap;
    for (auto it = manifest["textures"].begin(); it != manifest["textures"].end(); ++it) {
        AssetId oldId{};
        try { oldId = std::stoull(it.key()); } catch (...) { throw std::runtime_error("Map texture id is invalid."); }
        if (!it.value().is_string()) throw std::runtime_error("Map texture path is invalid.");
        const std::filesystem::path relative(it.value().get<std::string>());
        if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
            throw std::runtime_error("Absolute paths are forbidden in map asset manifests.");
        const auto normalized = relative.lexically_normal();
        if (normalized.empty() || std::any_of(normalized.begin(), normalized.end(), [](const auto& part) { return part == ".."; }))
            throw std::runtime_error("Map texture path escapes the asset folder.");
        std::string error;
        const AssetId newId = state.textureCache->acquire(directory / normalized, &error);
        if (!newId) throw std::runtime_error("Could not load a map texture: " + error);
        textureRemap.emplace(oldId, newId);
    }
    const auto remapRef = [&](TextureReference& ref) {
        if (!ref.assetId) return;
        const auto found = textureRemap.find(ref.assetId);
        if (found == textureRemap.end()) throw std::runtime_error("Map material references a missing texture entry.");
        ref.assetId = found->second;
    };
    for (const auto& encoded : manifest["materials"]) {
        auto material = MaterialFromJson(encoded);
        remapRef(material.albedoMap); remapRef(material.normalMap); remapRef(material.roughnessMap);
        remapRef(material.metalnessMap); remapRef(material.emissiveMap); remapRef(material.ambientOcclusionMap);
        const auto id = material.id;
        state.materials[id] = material;
        state.raylibMaterials[id] = LoadMaterialDefault();
        blubox::engine::render::RaylibMaterialAdapter adapter(*state.textureCache);
        adapter.apply(state.raylibMaterials[id], material);
    }
    for (auto& entity : state.map.entities) {
        if (entity.kind == "decal") {
            const auto oldId = entity.properties.value("textureId", std::uint64_t{0});
            if (oldId) {
                const auto found = textureRemap.find(oldId);
                if (found == textureRemap.end()) throw std::runtime_error("Decal references a missing texture entry.");
                entity.properties["textureId"] = found->second;
            }
        }
        for (const char* key : {"albedoMap", "normalMap", "roughnessMap", "metalnessMap", "emissiveMap"}) {
            const auto oldId = entity.properties.value(key, std::uint64_t{0});
            if (!oldId) continue;
            const auto found = textureRemap.find(oldId);
            if (found == textureRemap.end()) throw std::runtime_error("Part material references a missing texture entry.");
            entity.properties[key] = found->second;
        }
        if (entity.kind != "meshpart") continue;
        const auto id = entity.properties.value("meshAssetId", std::uint64_t{0});
        if (!id) continue;
        std::string error;
        const auto meshPath = directory / "meshes" / (std::to_string(id) + ".mesh");
        if (!state.meshCache.get(id) && !state.meshCache.loadAsset(meshPath, id, error))
            throw std::runtime_error("Could not restore imported mesh: " + error);
        const auto mesh = state.meshCache.get(id);
        if (!mesh || !state.raylibMeshes.upload(id, *mesh, &error))
            throw std::runtime_error("Could not upload imported mesh: " + error);
    }
}

bool SaveMap(StudioState& state, bool choosePath) {
    auto markScriptSourcesSaved = [&] {
        for (const auto& entity : state.map.entities) if (IsScriptObject(entity)) state.scriptSavedSource[entity.id] = entity.luauScript;
        state.dirtyScriptIds.clear();
    };
    if (!choosePath && state.authenticated && !state.websiteToken.empty()) {
        const bool containsLocalAssets = std::any_of(state.map.entities.begin(), state.map.entities.end(), [](const Entity& entity) {
            return entity.kind == "meshpart" || entity.kind == "decal" || entity.properties.value("albedoMap", std::uint64_t{0}) != 0 ||
                   entity.properties.value("normalMap", std::uint64_t{0}) != 0 ||
                   entity.properties.value("roughnessMap", std::uint64_t{0}) != 0 ||
                   entity.properties.value("metalnessMap", std::uint64_t{0}) != 0 ||
                   entity.properties.value("emissiveMap", std::uint64_t{0}) != 0;
        });
        if (containsLocalAssets) {
            SetStatus(state, "This map contains local mesh or texture assets. Save a local .blubox copy so its .assets folder is included.", true);
            return false;
        }
        std::string error;
        if (!state.api.PublishMap(state.map, state.websiteToken, error)) {
            SetStatus(state, "Cloud save failed: " + error, true);
            return false;
        }
        state.cloudWorldId = state.map.worldId;
        state.dirty = false;
        markScriptSourcesSaved();
        SetStatus(state, "Saved to Blubox Cloud.");
        return true;
    }

    std::filesystem::path path = state.currentFilePath;
    if (choosePath || path.empty()) {
        auto selected = ChooseMapPath(true);
        if (!selected) return false;
        path = std::move(*selected);
    }
    try {
        std::string assetError;
        if (!SaveMapAssets(state, path, assetError)) throw std::runtime_error(assetError);
        const auto json = state.map.ToJson();
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Could not open the selected file for writing.");
        output << json.dump(2) << '\n';
        if (!output) throw std::runtime_error("The map file could not be written completely.");
        state.currentFilePath = path;
        state.dirty = false;
        markScriptSourcesSaved();
        SetStatus(state, "Saved " + PathToUtf8(path));
        return true;
    } catch (const std::exception& exception) {
        SetStatus(state, std::string("Save failed: ") + exception.what(), true);
        return false;
    }
}

void OpenMap(StudioState& state) {
    auto selected = ChooseMapPath(false);
    if (!selected) return;
    try {
        std::ifstream input(*selected, std::ios::binary);
        if (!input) throw std::runtime_error("Could not open the selected map file.");
        nlohmann::json json;
        input >> json;
        MapDocument opened = MapDocument::FromJson(json);
        ReleaseProjectAssets(state);
        state.map = std::move(opened);
        LoadMapAssets(state, *selected);
        state.openScriptIds.clear();
        state.scriptEditors.clear();
        state.scriptSavedSource.clear();
        state.dirtyScriptIds.clear();
        for (const auto& entity : state.map.entities) if (entity.kind == "script" || entity.kind == "localscript") state.scriptSavedSource[entity.id] = entity.luauScript;
        state.currentFilePath = *selected;
        state.cloudWorldId = state.map.worldId;
        state.selectedEntity = -1;
        state.selectedService.clear();
        state.dirty = false;
        CreateScriptEngine(state);
        SetStatus(state, "Opened " + PathToUtf8(*selected));
    } catch (const std::exception& exception) {
        SetStatus(state, std::string("Open failed: ") + exception.what(), true);
    }
}

void NewMap(StudioState& state) {
    try {
        ReleaseProjectAssets(state);
        state.map = MapDocument::Create("Untitled World");
        state.openScriptIds.clear();
        state.scriptEditors.clear();
        state.currentFilePath.clear();
        state.cloudWorldId.clear();
        state.selectedEntity = -1;
        state.selectedService.clear();
        state.dirty = false;
        CreateScriptEngine(state);
        SetStatus(state, "New world ready. Use File > Save to choose a .blubox file.");
    } catch (const std::exception& exception) { SetStatus(state, exception.what(), true); }
}

void BeginSessionValidation(StudioState& state, std::string token) {
    blubox::studio::LogDiagnostic("INFO", "Starting website session validation.");
    state.authError.clear();
    state.validatingSession = true;
    state.validationTask = std::async(std::launch::async, [api = state.api, token = std::move(token)]() mutable {
        SessionValidation result;
        result.token = std::move(token);
        result.valid = api.ValidateSession(result.token, result.displayName, result.error);
        return result;
    });
}

void PollSessionValidation(StudioState& state) {
    if (state.handoffPollTask.valid() &&
        state.handoffPollTask.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        StudioHandoffPoll result = state.handoffPollTask.get();
        if (!result.error.empty()) {
            if (result.error != state.lastHandoffError) {
                blubox::studio::LogDiagnostic("WARNING", result.error);
                state.lastHandoffError = result.error;
                state.authError = result.error;
            }
            std::fill(result.token.begin(), result.token.end(), '\0');
        } else if (!result.token.empty()) {
            state.lastHandoffError.clear();
            if (result.token != state.websiteToken) {
                blubox::studio::LogDiagnostic("INFO", "Website delivered the browser session through the one-time handoff; token redacted.");
                state.pendingHandoffId.clear();
                state.handoffDeadline = {};
                std::string saveError;
                if (SessionStore::Save(result.token, saveError)) {
                    state.observedSessionFileTime = SessionStore::ModifiedTime();
                    if (state.validatingSession) {
                        std::fill(state.pendingHandoffToken.begin(), state.pendingHandoffToken.end(), '\0');
                        state.pendingHandoffToken = std::move(result.token);
                    } else {
                        BeginSessionValidation(state, std::move(result.token));
                    }
                } else {
                    std::fill(result.token.begin(), result.token.end(), '\0');
                    state.authError = saveError;
                    blubox::studio::LogDiagnostic("ERROR", "Could not protect the delivered Studio session: " + saveError);
                }
            } else {
                std::fill(result.token.begin(), result.token.end(), '\0');
            }
        } else {
            state.lastHandoffError.clear();
        }
    }

    const auto handoffNow = std::chrono::steady_clock::now();
    if (!state.pendingHandoffId.empty() && !state.handoffPollTask.valid() &&
        handoffNow >= state.nextHandoffPoll) {
        if (handoffNow >= state.handoffDeadline) {
            state.pendingHandoffId.clear();
            state.loginBrowserOpened = false;
            state.authError = "Studio sign-in expired. Click the sign-in button to try again.";
            blubox::studio::LogDiagnostic("WARNING", "Browser-to-Studio sign-in handoff expired before a session arrived.");
        } else {
            const std::string handoffId = state.pendingHandoffId;
            const WebsiteApi api = state.api;
            state.nextHandoffPoll = handoffNow + std::chrono::seconds(1);
            state.handoffPollTask = std::async(std::launch::async, [api, handoffId] {
                StudioHandoffPoll result;
                api.PollStudioHandoff(handoffId, result.token, result.error);
                return result;
            });
        }
    }

    if (state.validatingSession && state.validationTask.valid() &&
        state.validationTask.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        SessionValidation result = state.validationTask.get();
        state.validatingSession = false;
        const bool supersededByInteractiveLogin = state.forceInteractiveLoginAfterValidation;
        state.forceInteractiveLoginAfterValidation = false;
        if (supersededByInteractiveLogin) {
            blubox::studio::LogDiagnostic("INFO", "Ignored the saved-session result because a new browser sign-in was requested.");
            std::fill(result.token.begin(), result.token.end(), '\0');
            if (!state.pendingHandoffToken.empty()) {
                std::string token = std::move(state.pendingHandoffToken);
                state.pendingHandoffToken.clear();
                BeginSessionValidation(state, std::move(token));
            }
        } else if (result.valid) {
            blubox::studio::LogDiagnostic("INFO", "Website session validation succeeded.");
            TraceLog(LOG_INFO, "Studio sign-in validated for %s.", result.displayName.c_str());
            state.websiteToken = std::move(result.token);
            state.displayName = result.displayName.empty() ? "Blubox user" : std::move(result.displayName);
            state.authenticated = true;
            state.pendingHandoffId.clear();
            state.handoffDeadline = {};
            state.projectOpen = true;
            CreateScriptEngine(state);
            SetStatus(state, "Signed in as " + state.displayName + ".");
        } else {
            blubox::studio::LogDiagnostic("ERROR", result.error);
            TraceLog(LOG_ERROR, "Studio sign-in validation failed: %s", result.error.c_str());
            std::fill(result.token.begin(), result.token.end(), '\0');
            std::fill(state.websiteToken.begin(), state.websiteToken.end(), '\0');
            state.websiteToken.clear();
            state.displayName.clear();
            state.authenticated = false;
            state.authError = result.error.empty() ? "Sign-in could not be validated. Please try again." : result.error;
            std::string ignored;
            SessionStore::Clear(ignored);
            state.observedSessionFileTime = SessionStore::ModifiedTime();
        }
    }

    const auto now = std::chrono::steady_clock::now();
    if (state.authenticated || state.validatingSession || now < state.nextSessionPoll) return;
    state.nextSessionPoll = now + std::chrono::milliseconds(500);
    const bool updateSignaled = SessionStore::ConsumeUpdateSignal();
    const auto modified = SessionStore::ModifiedTime();
    if (!updateSignaled && modified == state.observedSessionFileTime) return;
    blubox::studio::LogDiagnostic("INFO", updateSignaled
        ? "Received the cross-process sign-in signal."
        : "Detected a changed protected session file.");
    state.observedSessionFileTime = modified;
    if (!modified) {
        blubox::studio::LogDiagnostic("WARNING", "Sign-in signal arrived but no protected session file exists.");
        return;
    }
    std::string error;
    auto token = SessionStore::Load(error);
    if (!token) {
        if (!error.empty()) blubox::studio::LogDiagnostic("ERROR", error);
        if (!error.empty()) state.authError = error;
        return;
    }
    if (*token != state.websiteToken) {
        blubox::studio::LogDiagnostic("INFO", "Loaded protected session; token value redacted.");
        state.pendingHandoffId.clear();
        state.handoffDeadline = {};
        TraceLog(LOG_INFO, "Studio received a sign-in handoff; validating session with %s.",
                 state.api.baseUrl().c_str());
        BeginSessionValidation(state, std::move(*token));
    }
    else std::fill(token->begin(), token->end(), '\0');
}

void StartBrowserLogin(StudioState& state) {
    // Reuse a live handoff when the user closed the browser tab. The website
    // is still polling this ID, so reopening it with a new ID would strand the
    // original request until its timeout.
    if (!state.pendingHandoffId.empty()) {
        std::string error;
        if (blubox::studio::OpenStudioLoginPage(state.pendingHandoffId, error)) {
            state.loginBrowserOpened = true;
            state.authError.clear();
        } else {
            state.authError = std::move(error);
        }
        return;
    }
    const bool supersedeSavedSessionCheck = state.validatingSession;
    std::string error;
    std::string handoffId;
    if (!blubox::studio::CreateStudioHandoffId(handoffId, error)) {
        state.authError = std::move(error);
        return;
    }
    if (blubox::studio::OpenStudioLoginPage(handoffId, error)) {
        state.pendingHandoffId = std::move(handoffId);
        state.nextHandoffPoll = std::chrono::steady_clock::now();
        state.handoffDeadline = state.nextHandoffPoll + std::chrono::minutes(3);
        state.lastHandoffError.clear();
        state.loginBrowserOpened = true;
        state.forceInteractiveLoginAfterValidation = supersedeSavedSessionCheck;
        state.authError.clear();
        std::string clearError;
        if (SessionStore::Clear(clearError)) {
            state.observedSessionFileTime = SessionStore::ModifiedTime();
        } else {
            state.authError = "Could not clear the previous saved session: " + clearError;
        }
    } else state.authError = std::move(error);
}

void Logout(StudioState& state) {
    if (state.projectOpen) {
        std::string error;
        if (!state.api.PublishMap(state.map, state.websiteToken, error)) {
            SetStatus(state, "Logout paused: safety save failed. " + error, true);
            return;
        }
    }
    std::string error;
    if (!SessionStore::Clear(error)) {
        SetStatus(state, "Logout paused: could not clear the saved session. " + error, true);
        return;
    }
    state.scripts.reset();
    state.map = MapDocument::Create("My First World");
    state.openScriptIds.clear();
    state.scriptEditors.clear();
    state.selectedEntity = -1;
    state.selectedService.clear();
    state.cloudWorldId.clear();
    state.projectOpen = false;
    state.authenticated = false;
    state.validatingSession = false;
    state.forceInteractiveLoginAfterValidation = false;
    state.loginBrowserOpened = false;
    state.pendingHandoffId.clear();
    state.handoffDeadline = {};
    state.authError.clear();
    state.displayName.clear();
    std::fill(state.websiteToken.begin(), state.websiteToken.end(), '\0');
    state.websiteToken.clear();
    std::fill(state.pendingHandoffToken.begin(), state.pendingHandoffToken.end(), '\0');
    state.pendingHandoffToken.clear();
    state.observedSessionFileTime = SessionStore::ModifiedTime();
    SetStatus(state, "Signed out. Sign in to open a project.");
}

void SetGameCursorCaptured(StudioState& state, bool captured) {
    if (captured) {
        state.inputRouter.captureGameViewport();
        if (!state.gameCursorCaptured) DisableCursor();
    } else {
        state.inputRouter.releaseGameViewport();
        if (state.gameCursorCaptured) EnableCursor();
    }
    state.gameCursorCaptured = captured;
}

void EnsureViewport(StudioState& state) {
    const int width = std::clamp(state.requestedWidth, 320, 3840);
    const int height = std::clamp(state.requestedHeight, 240, 2160);
    if (state.viewport.texture.id != 0 && state.viewport.texture.width == width &&
        state.viewport.texture.height == height) return;
    if (state.viewport.texture.id != 0) UnloadRenderTexture(state.viewport);
    state.viewport = LoadRenderTexture(width, height);
    SetTextureFilter(state.viewport.texture, TEXTURE_FILTER_BILINEAR);
}

void DrawWorld(const StudioState& state, const MapDocument& world, bool simulationView = false,
               bool showEditorGrid = true) {
    BeginTextureMode(state.viewport);
    ClearBackground(Color{123, 177, 207, 255});
    BeginMode3D(state.camera);
    if (showEditorGrid) DrawGrid(48, 1.0f);
    for (std::size_t i = 0; i < world.entities.size(); ++i) {
        const Entity& entity = world.entities[i];
        if (!entity.visible) continue;
        if (entity.kind == "meshpart") {
            if (!IsWorkspaceDescendant(world, entity)) continue;
            const auto assetId = entity.properties.value("meshAssetId", std::uint64_t{0});
            const Mesh* mesh = state.raylibMeshes.get(assetId);
            if (!mesh) continue;
            const auto materialId = entity.properties.value("materialId", std::uint64_t{0});
            const auto material = state.raylibMaterials.find(materialId);
            if (material == state.raylibMaterials.end()) continue;

            // Compose the complete instance chain from Workspace to the mesh.
            Material renderMaterial = material->second;
            if (entity.properties.contains("transparency") && entity.properties["transparency"].is_number()) {
                const float transparency = std::clamp(entity.properties["transparency"].get<float>(), 0.0f, 1.0f);
                auto& diffuse = renderMaterial.maps[MATERIAL_MAP_ALBEDO].color;
                diffuse.a = static_cast<unsigned char>(std::lround(static_cast<float>(diffuse.a) * (1.0f - transparency)));
            }
            DrawMesh(*mesh, renderMaterial, WorldMatrix(world, entity));
            continue;
        }
        if (entity.kind != "block" && entity.kind != "sphere" && entity.kind != "spawnlocation" &&
            entity.kind != "wedgepart" && entity.kind != "cornerwedgepart") continue;
        if (!IsWorkspaceDescendant(world, entity)) continue;
        const auto& t = entity.transform;
        const bool selected = !simulationView && state.selectedEntity == static_cast<int>(i);
        Color color = selected ? Color{255, 207, 92, 255}
                               : Color{entity.color[0], entity.color[1], entity.color[2], entity.color[3]};
        if (!selected && entity.properties.contains("transparency") && entity.properties["transparency"].is_number()) {
            const float transparency = std::clamp(entity.properties["transparency"].get<float>(), 0.0f, 1.0f);
            color.a = static_cast<unsigned char>(std::lround(255.0f * (1.0f - transparency)));
        }
        rlPushMatrix();
        const Matrix transform = WorldMatrix(world, entity);
        rlMultMatrixf(MatrixToFloatV(transform).v);
        if (entity.kind == "sphere") DrawSphere(Vector3{0.0f, 0.0f, 0.0f}, 0.5f, color);
        else DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{1.0f, 1.0f, 1.0f}, color);
        if (!simulationView && entity.kind == "spawnlocation") {
            // A bright inset disk makes the SpawnLocation recognizable as an
            // editor object while keeping the actual pad a normal transformable part.
            const float inverseYScale = 1.0f / std::max(0.005f, std::abs(t.scale[1]));
            DrawCylinder(Vector3{0.0f, 0.5f + 0.025f * inverseYScale, 0.0f}, 0.16f, 0.16f,
                         0.05f * inverseYScale, 24, Color{225, 245, 235, 255});
        }
        rlPopMatrix();
    }
    if (state.decalQuad.vaoId != 0 || state.decalQuad.vboId != nullptr) {
        for (const Entity& decal : world.entities) {
            if (decal.kind != "decal" || !decal.visible) continue;
            const auto textureId = decal.properties.value("textureId", std::uint64_t{0});
            const auto material = state.decalMaterials.find(decal.id);
            if (!textureId || material == state.decalMaterials.end()) continue;
            const auto parent = std::find_if(world.entities.begin(), world.entities.end(), [&](const Entity& item) { return item.id == decal.parent; });
            if (parent == world.entities.end() || (parent->kind != "block" && parent->kind != "meshpart")) continue;
            const auto size = decal.properties.value("size", nlohmann::json::array({1.0, 1.0}));
            if (!size.is_array() || size.size() != 2) continue;
            const float width = std::clamp(size[0].get<float>(), 0.001f, 100000.0f);
            const float depth = std::clamp(size[1].get<float>(), 0.001f, 100000.0f);
            const float rotation = decal.properties.value("rotationDegrees", 0.0f) * DEG2RAD;
            const Matrix local = MatrixMultiply(
                MatrixMultiply(MatrixScale(width, 1.0f, depth), MatrixRotateY(rotation)),
                MatrixTranslate(0.0f, parent->transform.scale[1] * 0.5f + 0.002f, 0.0f));
            Material decalMaterial = material->second;
            const float transparency = std::clamp(decal.properties.value("transparency", 0.0f), 0.0f, 1.0f);
            const auto tint = decal.properties.value("tint", nlohmann::json::array({255, 255, 255, 255}));
            const float tintAlpha = tint.is_array() && tint.size() >= 4 && tint[3].is_number_integer()
                ? static_cast<float>(std::clamp(tint[3].get<int>(), 0, 255)) / 255.0f : 1.0f;
            auto& decalColor = decalMaterial.maps[MATERIAL_MAP_ALBEDO].color;
            decalColor.a = static_cast<unsigned char>(std::lround(static_cast<float>(decalColor.a) * tintAlpha * (1.0f - transparency)));
            DrawMesh(state.decalQuad, decalMaterial, MatrixMultiply(local, WorldMatrix(world, *parent)));
        }
    }
    EndMode3D();
    const float uiWidth = static_cast<float>(std::max(1, state.viewport.texture.width));
    const float uiHeight = static_cast<float>(std::max(1, state.viewport.texture.height));
    std::unordered_map<std::string, const Entity*> uiById;
    uiById.reserve(world.entities.size());
    for (const Entity& entity : world.entities) uiById.emplace(entity.id, &entity);
    std::function<void(const std::string&)> drawGuiChildren;
    drawGuiChildren = [&](const std::string& parent) {
        for (const Entity& entity : world.entities) {
            if (entity.parent != parent || entity.kind == "screengui" ||
                entity.kind == "script" || entity.kind == "localscript") continue;
            if (entity.kind != "frame" && entity.kind != "textlabel" && entity.kind != "textbutton") continue;
            const auto resolved = GetGuiElementRect(entity, uiById, uiWidth, uiHeight);
            if (!resolved) continue;
            const int x = static_cast<int>(std::lround(resolved->x));
            const int y = static_cast<int>(std::lround(resolved->y));
            const int width = std::max(1, static_cast<int>(std::lround(resolved->width)));
            const int height = std::max(1, static_cast<int>(std::lround(resolved->height)));
            const auto readColor = [](const nlohmann::json& values, Color fallback) {
                if (!values.is_array() || values.size() != 4) return fallback;
                return Color{static_cast<unsigned char>(std::clamp(values[0].get<int>(), 0, 255)),
                             static_cast<unsigned char>(std::clamp(values[1].get<int>(), 0, 255)),
                             static_cast<unsigned char>(std::clamp(values[2].get<int>(), 0, 255)),
                             static_cast<unsigned char>(std::clamp(values[3].get<int>(), 0, 255))};
            };
            const auto applyTransparency = [&](Color color, const char* key) {
                if (entity.properties.contains(key) && entity.properties[key].is_number()) {
                    const float transparency = std::clamp(entity.properties[key].get<float>(), 0.0f, 1.0f);
                    color.a = static_cast<unsigned char>(std::lround(255.0f * (1.0f - transparency)));
                }
                return color;
            };
            const Color background = applyTransparency(
                readColor(entity.properties.value("backgroundColor", nlohmann::json::array()), Color{34, 44, 58, 230}),
                "backgroundTransparency");
            DrawRectangle(x, y, width, height, background);
            if (entity.kind == "textlabel" || entity.kind == "textbutton") {
                const std::string source = entity.properties.value("text", std::string{});
                Color textColor = applyTransparency(
                    readColor(entity.properties.value("textColor", nlohmann::json::array()), WHITE), "textTransparency");
                const bool scaleWithViewport = entity.properties.value("scaleWithViewport", true);
                const float viewportFontScale = scaleWithViewport
                    ? std::min(uiWidth / 1280.0f, uiHeight / 720.0f) : 1.0f;
                float fontSize = std::clamp(entity.properties.value("fontSize", 20.0f) * viewportFontScale, 6.0f, 144.0f);
                const float textWidth = std::max(1.0f, static_cast<float>(width) - 20.0f);
                const float textHeight = std::max(1.0f, static_cast<float>(height) - 8.0f);
                const bool wrap = entity.properties.value("textWrapped", false);
                const bool truncate = entity.properties.value("textTruncate", false);
                const bool textScaled = entity.properties.value("textScaled", false);
                const auto popLastCodepoint = [](std::string& value) {
                    if (value.empty()) return;
                    std::size_t first = value.size() - 1;
                    while (first > 0 && (static_cast<unsigned char>(value[first]) & 0xc0u) == 0x80u) --first;
                    value.resize(first);
                };
                auto measure = [](const std::string& value, float size) {
                    return MeasureTextEx(GetFontDefault(), value.c_str(), size, 1.0f);
                };
                auto wrapText = [&](float size) {
                    std::vector<std::string> lines;
                    if (!wrap) {
                        std::string line = source;
                        if (truncate && measure(line, size).x > textWidth) {
                            constexpr std::string_view suffix = "...";
                            while (!line.empty() && measure(line + std::string(suffix), size).x > textWidth) popLastCodepoint(line);
                            line += suffix;
                        }
                        lines.push_back(std::move(line));
                        return lines;
                    }
                    std::string line, word;
                    const auto pushWord = [&] {
                        if (word.empty()) return;
                        const std::string candidate = line.empty() ? word : line + " " + word;
                        if (!line.empty() && measure(candidate, size).x > textWidth) {
                            lines.push_back(std::move(line));
                            line = std::move(word);
                        } else line = candidate;
                        word.clear();
                    };
                    for (char ch : source) {
                        if (std::isspace(static_cast<unsigned char>(ch))) pushWord();
                        else word.push_back(ch);
                    }
                    pushWord();
                    if (!line.empty() || lines.empty()) lines.push_back(std::move(line));
                    if (truncate) {
                        const float lineHeight = size * 1.2f;
                        const std::size_t maxLines = std::max<std::size_t>(1, static_cast<std::size_t>(textHeight / lineHeight));
                        if (lines.size() > maxLines) {
                            lines.resize(maxLines);
                            std::string& last = lines.back();
                            while (!last.empty() && measure(last + "...", size).x > textWidth) popLastCodepoint(last);
                            last += "...";
                        }
                    }
                    return lines;
                };
                auto lines = wrapText(fontSize);
                if (textScaled && !source.empty()) {
                    float widest = 1.0f;
                    for (const auto& line : lines) widest = std::max(widest, measure(line, fontSize).x);
                    const float totalHeight = std::max(1.0f, fontSize * 1.2f * static_cast<float>(lines.size()));
                    const float fit = std::min(textWidth / widest, textHeight / totalHeight);
                    fontSize = std::clamp(fontSize * fit, 6.0f, 144.0f);
                    lines = wrapText(fontSize);
                }
                const float lineHeight = fontSize * 1.2f;
                const float totalHeight = lineHeight * static_cast<float>(lines.size());
                const std::string horizontal = entity.properties.value("textXAlignment", std::string("Left"));
                const std::string vertical = entity.properties.value("textYAlignment", std::string("Center"));
                float textY = static_cast<float>(y) + 4.0f;
                if (vertical == "Center") textY = static_cast<float>(y) + (static_cast<float>(height) - totalHeight) * 0.5f;
                else if (vertical == "Bottom") textY = static_cast<float>(y + height) - totalHeight - 4.0f;
                BeginScissorMode(x, y, width, height);
                for (const auto& line : lines) {
                    const float lineWidth = measure(line, fontSize).x;
                    float textX = static_cast<float>(x) + 10.0f;
                    if (horizontal == "Center") textX = static_cast<float>(x) + (static_cast<float>(width) - lineWidth) * 0.5f;
                    else if (horizontal == "Right") textX = static_cast<float>(x + width) - lineWidth - 10.0f;
                    DrawTextEx(GetFontDefault(), line.c_str(), Vector2{textX, textY}, fontSize, 1.0f, textColor);
                    textY += lineHeight;
                }
                EndScissorMode();
            }
            if (entity.kind == "frame" || entity.kind == "textlabel" || entity.kind == "textbutton") drawGuiChildren(entity.id);
        }
    };
    for (const Entity& screenGui : world.entities) {
        if (screenGui.kind == "screengui" && screenGui.parent == "StarterGui" &&
            screenGui.properties.value("enabled", true)) {
            drawGuiChildren(screenGui.id);
        }
    }
    EndTextureMode();
}

void DrawTopBar(StudioState& state) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New world", "Ctrl+N")) state.pendingNewMap = true;
        if (ImGui::MenuItem("Open...", "Ctrl+O")) state.pendingOpenMap = true;
        if (ImGui::MenuItem("Import 3D model...", nullptr, false, state.projectOpen && !state.modelImportTask.valid())) QueueModelImport(state);
        ImGui::Separator();
        if (ImGui::MenuItem(state.authenticated ? "Save to Blubox" : "Save", "Ctrl+S")) SaveMap(state, false);
        if (ImGui::MenuItem("Save a local copy...", "Ctrl+Shift+S")) SaveMap(state, true);
        ImGui::Separator();
        if (ImGui::MenuItem("Publish to Website...")) {
            state.publishTitle = state.map.name;
            state.publishDescription.clear();
            state.publishError.clear();
            state.showPublishDialog = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) state.exitRequested = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Explorer", nullptr, &state.showExplorer);
        ImGui::MenuItem("Properties", nullptr, &state.showProperties);
        ImGui::MenuItem("Viewport", nullptr, &state.showViewport);
        ImGui::MenuItem("Output", nullptr, &state.showOutput);
        ImGui::Separator();
        ImGui::MenuItem("World settings", nullptr, &state.showWorldPanel);
        ImGui::MenuItem("Cloud publishing", nullptr, &state.showCloudPanel);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Insert")) {
        if (ImGui::MenuItem("Block")) AddEditorObject(state, "block", "Workspace");
        if (ImGui::MenuItem("Sphere")) AddEditorObject(state, "sphere", "Workspace");
        if (ImGui::MenuItem("SpawnLocation")) AddEditorObject(state, "spawnlocation", "Workspace");
        if (state.selectedEntity >= 0 && state.selectedEntity < static_cast<int>(state.map.entities.size())) {
            const auto& selected = state.map.entities[static_cast<std::size_t>(state.selectedEntity)];
            if ((selected.kind == "block" || selected.kind == "meshpart") && ImGui::MenuItem("Decal on selected part")) AddEditorObject(state, "decal", selected.id);
        }
        if (ImGui::MenuItem("3D Model...", nullptr, false, state.projectOpen && !state.modelImportTask.valid())) QueueModelImport(state);
        ImGui::Separator();
        if (ImGui::MenuItem("Script", nullptr, false, state.projectOpen)) AddScriptForSelection(state, false);
        if (ImGui::MenuItem("LocalScript", nullptr, false, state.projectOpen)) AddScriptForSelection(state, true);
        ImGui::EndMenu();
    }
    if (ImGui::Button(state.authenticated ? "Save to Blubox" : "Save")) SaveMap(state, false);
    if (!state.playMode) state.playMode = std::make_unique<blubox::engine::play::PlayModeBridge>(state.map);
    using blubox::engine::play::PlayModeState;
    auto playState = state.playMode->state();
    if (playState == PlayModeState::Stopped || playState == PlayModeState::Failed) {
        if (playState == PlayModeState::Failed) state.playMode->stop();
        auto startMode = [&](blubox::engine::play::PlayModeType mode) {
            blubox::engine::play::PlayConfig config;
            config.mode = mode;
            if (mode == blubox::engine::play::PlayModeType::PlayHere) {
                const Vector3 direction = Vector3Normalize(Vector3Subtract(state.camera.target, state.camera.position));
                const Vector3 position = Vector3Add(state.camera.position, Vector3Scale(direction, 5.0f));
                config.requestedSpawn = {position.x, std::max(2.0f, position.y), position.z};
                config.requestedForward = {direction.x, direction.y, direction.z};
            }
            blubox::engine::play::PlayError error;
            if (state.cameraLookCaptured) {
                EnableCursor();
                state.cameraLookCaptured = false;
            }
            state.editorCamera = state.camera;
            state.editorCameraSaved = true;
            if (state.playMode->start(config, error)) {
                if (mode != blubox::engine::play::PlayModeType::Run) {
                    const Vector3 look = Vector3Normalize(Vector3Subtract(state.camera.target, state.camera.position));
                    state.gameCamera = {};
                    // The client uses yaw 0 for -Z and stores orbit elevation as
                    // positive pitch, opposite the look vector's vertical sign.
                    state.gameCamera.yaw = std::atan2(look.x, -look.z);
                    state.gameCamera.pitch = std::clamp(-std::asin(std::clamp(look.y, -1.0f, 1.0f)), -0.15f, 0.85f);
                    state.gameCamera.distance = 7.0f;
                    SetGameCursorCaptured(state, true);
                } else {
                    SetGameCursorCaptured(state, false);
                }
                const char* label = mode == blubox::engine::play::PlayModeType::Run ? "Run" : mode == blubox::engine::play::PlayModeType::PlayHere ? "Play Here" : "Play";
                AppendOutput(state, "INFO", std::string(label) + " started in an isolated runtime world.");
                SetStatus(state, std::string(label) + " running");
            } else {
                AppendOutput(state, "ERROR", error.message);
                SetStatus(state, "Could not start simulation", true);
            }
        };
        if (ImGui::Button("Play  v")) ImGui::OpenPopup("PlayModeMenu");
        if (ImGui::BeginPopup("PlayModeMenu")) {
            if (ImGui::MenuItem("Play", "F5")) startMode(blubox::engine::play::PlayModeType::Play);
            if (ImGui::MenuItem("Play Here")) startMode(blubox::engine::play::PlayModeType::PlayHere);
            if (ImGui::MenuItem("Run", "F8")) startMode(blubox::engine::play::PlayModeType::Run);
            ImGui::EndPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) startMode(blubox::engine::play::PlayModeType::Play);
        if (ImGui::IsKeyPressed(ImGuiKey_F8, false)) startMode(blubox::engine::play::PlayModeType::Run);
    } else {
        if (playState == PlayModeState::Paused) {
            if (ImGui::Button("Resume")) state.playMode->resume();
        } else if (playState == PlayModeState::Running) {
            if (ImGui::Button("Pause")) state.playMode->pause();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            state.playMode->stop();
            if (state.editorCameraSaved) { state.camera = state.editorCamera; state.editorCameraSaved = false; }
            SetGameCursorCaptured(state, false);
            AppendOutput(state, "INFO", "Local Play stopped; editor world remains unchanged.");
            SetStatus(state, "Local Play stopped");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", playState == PlayModeState::Paused ? "Paused" : (state.playMode->mode() == blubox::engine::play::PlayModeType::Run ? "Running · server" : "Running · server/client"));
    }
    if (ImGui::Button("Publish")) {
        state.publishTitle = state.map.name;
        state.publishDescription.clear();
        state.publishError.clear();
        state.showPublishDialog = true;
    }
    ImGui::TextDisabled("%s%s", state.map.name.c_str(), state.dirty ? " *" : "");
    const float statusWidth = ImGui::CalcTextSize(state.status.c_str()).x;
    const std::string accountLabel = state.displayName.empty() ? "Account" : state.displayName;
    const float accountWidth = ImGui::CalcTextSize(accountLabel.c_str()).x + 30.0f;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - statusWidth - accountWidth - 28.0f));
    if (ImGui::BeginMenu(accountLabel.c_str())) {
        if (ImGui::MenuItem("Log out")) Logout(state);
        ImGui::EndMenu();
    }
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - statusWidth - 12.0f));
    ImGui::TextColored(StatusColor(state.statusIsError), "%s", state.status.c_str());
    ImGui::EndMainMenuBar();
}

void SetupDefaultDockLayout(StudioState& state, const ImVec2& size) {
    const ImGuiID dockspaceId = ImGui::GetID("BluboxStudioDockspaceV2");
    if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr) {
        if (ImGuiWindow* viewportWindow = ImGui::FindWindowByName("Viewport")) {
            state.centralDockNode = viewportWindow->DockId;
        }
        return;
    }
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, size);
    ImGuiID center = dockspaceId;
    ImGuiID right = 0;
    ImGuiID properties = 0;
    ImGuiID outputPane = 0;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, &right, &center);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.48f, &properties, &right);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, &outputPane, &center);
    ImGui::DockBuilderDockWindow("Explorer", right);
    ImGui::DockBuilderDockWindow("Properties", properties);
    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderDockWindow("Output", outputPane);
    ImGui::DockBuilderFinish(dockspaceId);
    state.centralDockNode = center;
}

bool IsScriptObject(const Entity& entity);

void RunSelectedScript(StudioState& state) {
    if (state.selectedEntity < 0 || state.selectedEntity >= static_cast<int>(state.map.entities.size())) {
        SetStatus(state, "Select a Script or LocalScript in Explorer first.", true);
        return;
    }
    Entity& entity = state.map.entities[static_cast<std::size_t>(state.selectedEntity)];
    if (!IsScriptObject(entity)) {
        SetStatus(state, "Select a Script or LocalScript in Explorer first.", true);
        return;
    }
    std::string error;
    if (state.scripts && state.scripts->RunEntity(entity.id, error)) {
        const std::string message = entity.kind == "localscript"
            ? "LocalScript running in the editor preview sandbox."
            : "Script running in the editor preview sandbox.";
        AppendOutput(state, "INFO", message);
        SetStatus(state, message);
    } else SetStatus(state, error.empty() ? "The Luau preview is not ready." : error, true);
}

void DrawRibbon(StudioState& state) {
    const char* tabs[] = {"Home", "Model", "Script", "Test", "View"};
    ImGui::BeginChild("RibbonTabs", ImVec2(0.0f, 34.0f), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const float tabWidth = 76.0f;
    const float totalTabsWidth = tabWidth * static_cast<float>(std::size(tabs));
    ImGui::SetCursorPosX(std::max(8.0f, (ImGui::GetContentRegionAvail().x - totalTabsWidth) * 0.5f));
    for (const char* tab : tabs) {
        ImGui::PushID("RibbonTab");
        ImGui::PushID(tab);
        const ImVec2 tabPosition = ImGui::GetCursorScreenPos();
        const ImVec2 tabSize(tabWidth, 32.0f);
        const bool active = state.activeRibbonTab == tab;
        const bool clicked = ImGui::InvisibleButton("##tab", tabSize);
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (active) {
            draw->AddRectFilled(tabPosition, ImVec2(tabPosition.x + tabSize.x, tabPosition.y + tabSize.y),
                                ImGui::ColorConvertFloat4ToU32(ThemeColor(blubox::engine::ui::kDarkTheme.containerRaised)), 4.0f);
            draw->AddRectFilled(ImVec2(tabPosition.x + 10.0f, tabPosition.y + tabSize.y - 2.0f),
                                ImVec2(tabPosition.x + tabSize.x - 10.0f, tabPosition.y + tabSize.y),
                                ImGui::ColorConvertFloat4ToU32(ThemeColor(blubox::engine::ui::kDarkTheme.accent)));
        } else if (hovered) {
            draw->AddRectFilled(tabPosition, ImVec2(tabPosition.x + tabSize.x, tabPosition.y + tabSize.y),
                                ImGui::ColorConvertFloat4ToU32(ThemeColor(blubox::engine::ui::kDarkTheme.hover)), 4.0f);
        }
        const ImVec2 labelSize = ImGui::CalcTextSize(tab);
        draw->AddText(ImVec2(tabPosition.x + (tabSize.x - labelSize.x) * 0.5f,
                             tabPosition.y + (tabSize.y - labelSize.y) * 0.5f - 1.0f),
                      ImGui::ColorConvertFloat4ToU32(ThemeColor(active
                          ? blubox::engine::ui::kDarkTheme.textPrimary
                          : blubox::engine::ui::kDarkTheme.textMuted)), tab);
        if (clicked) state.activeRibbonTab = tab;
        ImGui::PopID();
        ImGui::PopID();
        ImGui::SameLine();
    }
    const std::string documentTitle = state.map.name + (state.dirty ? " *" : "");
    const float documentTitleWidth = ImGui::CalcTextSize(documentTitle.c_str()).x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - documentTitleWidth - 24.0f));
    ImGui::TextDisabled("%s", documentTitle.c_str());
    ImGui::EndChild();
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));
    ImGui::BeginChild("RibbonTools", ImVec2(0.0f, 42.0f), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const auto groupDivider = [] {
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0.0f, 8.0f);
    };

    const auto tool = [&](const char* label, ImGuizmo::OPERATION operation, const char* hint) {
        ImGui::PushID("TransformTool");
        ImGui::PushID(label);
        const bool selectTool = label[0] == 'S' && label[1] == 'e';
        const bool active = !selectTool && state.gizmoEnabled && state.gizmoOperation == operation;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ThemeColor(blubox::engine::ui::kDarkTheme.selected));
        if (ImGui::Button(label, ImVec2(74.0f, 30.0f))) {
            state.gizmoEnabled = !selectTool;
            if (!selectTool) state.gizmoOperation = operation;
            SetStatus(state, selectTool ? "Selection tool active." : std::string(label) + " tool selected (" + hint + ").");
        }
        if (active) ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::SameLine();
    };

    if (state.activeRibbonTab == "Home" || state.activeRibbonTab == "Model") {
        tool("Select", state.gizmoOperation, "click an object");
        tool("Move  W", ImGuizmo::TRANSLATE, "W");
        tool("Scale  R", ImGuizmo::SCALE, "R");
        tool("Rotate  E", ImGuizmo::ROTATE, "E");
        groupDivider();
        ImGui::PushID("InsertTools");
        if (ImGui::Button("Part", ImVec2(72.0f, 30.0f))) AddEditorObject(state, "block", "Workspace");
        ImGui::SameLine();
        if (state.activeRibbonTab == "Model") {
            ImGui::BeginDisabled(!state.projectOpen || state.modelImportTask.valid());
            if (ImGui::Button("Import 3D", ImVec2(96.0f, 30.0f))) QueueModelImport(state);
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Script", ImVec2(78.0f, 30.0f))) AddScriptForSelection(state, false);
        ImGui::SameLine();
        if (ImGui::Button("LocalScript", ImVec2(98.0f, 30.0f))) AddScriptForSelection(state, true);
        ImGui::PopID();
        groupDivider();
        ImGui::PushID("WindowTools");
        if (ImGui::Button("Explorer", ImVec2(82.0f, 30.0f))) state.showExplorer = !state.showExplorer;
        ImGui::SameLine();
        if (ImGui::Button("Properties", ImVec2(92.0f, 30.0f))) state.showProperties = !state.showProperties;
        ImGui::PopID();
    } else if (state.activeRibbonTab == "Script" || state.activeRibbonTab == "Test") {
        ImGui::PushID("ScriptTools");
        if (ImGui::Button("Run selected script", ImVec2(154.0f, 30.0f))) RunSelectedScript(state);
        ImGui::SameLine();
        if (ImGui::Button("Stop selected script", ImVec2(158.0f, 30.0f))) {
            if (state.selectedEntity >= 0 && state.selectedEntity < static_cast<int>(state.map.entities.size())) {
                state.scripts->StopEntity(state.map.entities[static_cast<std::size_t>(state.selectedEntity)].id);
                SetStatus(state, "Selected script stopped.");
            } else SetStatus(state, "Select a script to stop it.", true);
        }
        ImGui::SameLine(0.0f, 14.0f);
        ImGui::TextDisabled("Luau script tools");
        ImGui::PopID();
    } else {
        ImGui::PushID("ViewTools");
        if (ImGui::Button("Viewport", ImVec2(88.0f, 30.0f))) state.showViewport = !state.showViewport;
        ImGui::SameLine();
        if (ImGui::Button("Explorer", ImVec2(82.0f, 30.0f))) state.showExplorer = !state.showExplorer;
        ImGui::SameLine();
        if (ImGui::Button("Properties", ImVec2(92.0f, 30.0f))) state.showProperties = !state.showProperties;
        ImGui::SameLine();
        if (ImGui::Button("Output", ImVec2(74.0f, 30.0f))) state.showOutput = !state.showOutput;
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::Separator();
}

void BeginStudioWorkspace(StudioState& state) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##BluboxStudioWorkspace", nullptr, flags);
    ImGui::PopStyleVar();
    DrawRibbon(state);
    const ImVec2 dockSize = ImGui::GetContentRegionAvail();
    SetupDefaultDockLayout(state, dockSize);
    ImGui::DockSpace(ImGui::GetID("BluboxStudioDockspaceV2"), dockSize);
}

void DrawLoginModal(StudioState& state) {
    ImGui::OpenPopup("Sign in to Blubox Studio");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Sign in to Blubox Studio", nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Sign in with your Blubox account in your browser to open Studio.");
        ImGui::Spacing();
        if (state.validatingSession) {
            ImGui::TextDisabled("Checking your saved session...");
        }
        const char* loginButtonLabel = state.pendingHandoffId.empty()
            ? "Sign in on the web" : "Reopen sign-in page";
        if (ImGui::Button(loginButtonLabel, ImVec2(-1.0f, 42.0f))) {
            StartBrowserLogin(state);
        }
        if (!state.pendingHandoffId.empty()) {
            ImGui::TextDisabled("Waiting for sign-in. If you closed the tab, reopen it above.");
        } else if (state.loginBrowserOpened) {
            ImGui::TextWrapped("Finish signing in in your browser. Studio will continue automatically.");
        }
        if (!state.authError.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(StatusColor(true), "%s", state.authError.c_str());
        }
        ImGui::EndPopup();
    }
}

void DrawNewMapConfirmation(StudioState& state) {
    if (!state.pendingNewMap) return;
    ImGui::OpenPopup("Create a new world?");
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Create a new world?", &state.pendingNewMap,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize)) {
        if (state.dirty) {
            ImGui::TextWrapped("This world has unsaved changes. Save the .blubox file before creating a new world?");
            if (ImGui::Button("Save and continue", ImVec2(150, 0))) {
                if (SaveMap(state, false)) { NewMap(state); state.pendingNewMap = false; }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard changes", ImVec2(150, 0))) { NewMap(state); state.pendingNewMap = false; }
        } else {
            ImGui::TextWrapped("Start a fresh world with a baseplate and spawn point?");
            if (ImGui::Button("Create world", ImVec2(150, 0))) { NewMap(state); state.pendingNewMap = false; }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0))) state.pendingNewMap = false;
        ImGui::EndPopup();
    }
}

void DrawOpenMapConfirmation(StudioState& state) {
    if (!state.pendingOpenMap) return;
    if (!state.dirty) {
        state.pendingOpenMap = false;
        OpenMap(state);
        return;
    }
    ImGui::OpenPopup("Open another world?");
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Open another world?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("This world has unsaved changes. Save the .blubox file before opening another one?");
        if (ImGui::Button("Save and open...", ImVec2(150, 0))) {
            if (SaveMap(state, false)) { state.pendingOpenMap = false; OpenMap(state); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard changes", ImVec2(150, 0))) { state.pendingOpenMap = false; OpenMap(state); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0))) state.pendingOpenMap = false;
        ImGui::EndPopup();
    }
}

void DrawPublishDialog(StudioState& state) {
    if (!state.showPublishDialog) return;
    ImGui::OpenPopup("Publish game to Blubox");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(580.0f, 390.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Publish game to Blubox", &state.showPublishDialog,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextColored(Rgb(0x00d2ff), "PUBLISH YOUR GAME");
        ImGui::TextWrapped("Choose a name and tell players what your world is about. New games go through website review.");
        ImGui::Spacing();
        ImGui::TextDisabled("GAME NAME");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##PublishGameName", "My First World", &state.publishTitle);
        ImGui::Spacing();
        ImGui::TextDisabled("DESCRIPTION");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextMultiline("##PublishGameDescription", &state.publishDescription, ImVec2(-1.0f, 104.0f));
        ImGui::TextDisabled("%zu / 1000 characters", state.publishDescription.size());
        if (!state.publishError.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", state.publishError.c_str());
        }
        ImGui::Spacing();
        const bool canPublish = !state.publishing && !state.publishTitle.empty() &&
                                state.publishTitle.size() <= 64 && state.publishDescription.size() <= 1000;
        const float actionsWidth = 96.0f + ImGui::GetStyle().ItemSpacing.x + 152.0f;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
            ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - actionsWidth));
        if (ImGui::Button("Cancel", ImVec2(96.0f, 38.0f)) && !state.publishing) {
            state.showPublishDialog = false;
        }
        ImGui::SameLine();
        if (!canPublish) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button, Rgb(0x00d2ff));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(0x38e0ff));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Rgb(0x00a8cc));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(0x13131a));
        if (ImGui::Button(state.publishing ? "Publishing..." : "Publish game", ImVec2(152.0f, 38.0f))) {
            state.publishing = true;
            state.publishError.clear();
            std::string result;
            std::string error;
            if (state.api.PublishGame(state.map, state.publishTitle, state.publishDescription,
                                      state.websiteToken, result, error)) {
                state.dirty = true;
                SetStatus(state, result);
                state.showPublishDialog = false;
            } else {
                state.publishError = error;
                SetStatus(state, error, true);
            }
            state.publishing = false;
        }
        ImGui::PopStyleColor(4);
        if (!canPublish) ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

void HandleShortcuts(StudioState& state) {
    const bool control = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    if (!control || ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    if (ImGui::IsKeyPressed(ImGuiKey_N, false)) state.pendingNewMap = true;
    else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) state.pendingOpenMap = true;
    else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        const bool shift = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
        SaveMap(state, shift);
    }
}

bool IsScriptObject(const Entity& entity) {
    return entity.kind == "script" || entity.kind == "localscript";
}

void OpenScriptEditor(StudioState& state, const std::string& entityId) {
    const auto entity = std::find_if(state.map.entities.begin(), state.map.entities.end(),
        [&](const Entity& item) { return item.id == entityId; });
    if (entity == state.map.entities.end() || !IsScriptObject(*entity)) return;
    if (std::find(state.openScriptIds.begin(), state.openScriptIds.end(), entityId) == state.openScriptIds.end()) {
        state.openScriptIds.push_back(entityId);
    }
    auto& editor = state.scriptEditors[entityId];
    if (!editor) {
        editor = std::make_unique<TextEditor>();
        editor->SetLanguageDefinition(LuauLanguage());
        editor->SetPalette(LuauEditorPalette());
        editor->SetTabSize(4);
        editor->SetText(entity->luauScript);
    }
    state.scriptSavedSource.try_emplace(entityId, entity->luauScript);
    state.focusScriptId = entityId;
}

void SelectHierarchyItem(StudioState& state, std::size_t index) {
    state.selectedEntity = static_cast<int>(index);
    state.selectedService.clear();
    state.showProperties = true;
}

void DrawExplorerSelectionAccent() {
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    if (maximum.y <= minimum.y) return;
    ImGui::GetWindowDrawList()->AddRectFilled(
        minimum, ImVec2(minimum.x + 2.0f, maximum.y),
        ImGui::ColorConvertFloat4ToU32(ThemeColor(blubox::engine::ui::kDarkTheme.accent)));
}

void DrawExplorerAddButton(StudioState& state, const std::string& parentId) {
    // Keep the button inside the row's normal layout. Moving the cursor to a
    // fixed screen X can exceed a narrow dock window's parent bounds and trips
    // Dear ImGui's cursor-assertion checks.
    const float buttonWidth = ImGui::GetFrameHeight();
    const float rightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    ImGui::SameLine(0.0f, 0.0f);
    const float remaining = std::max(0.0f, rightEdge - ImGui::GetCursorScreenPos().x - buttonWidth);
    if (remaining > 0.0f) ImGui::Dummy(ImVec2(remaining, 0.0f));
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushID(parentId.c_str());
    if (ImGui::SmallButton("+")) ImGui::OpenPopup("Add child object");
    if (ImGui::BeginPopup("Add child object")) {
        ImGui::TextDisabled("ADD TO %s", parentId.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Part")) AddEditorObject(state, "block", parentId);
        if (ImGui::MenuItem("Sphere")) AddEditorObject(state, "sphere", parentId);
        if (ImGui::MenuItem("WedgePart")) AddEditorObject(state, "wedgepart", parentId);
        if (ImGui::MenuItem("SpawnLocation")) AddEditorObject(state, "spawnlocation", parentId);
        ImGui::Separator();
        if (ImGui::MenuItem("Script")) AddEditorObject(state, "script", parentId);
        if (ImGui::MenuItem("LocalScript")) AddEditorObject(state, "localscript", parentId);
        if (ImGui::MenuItem("Folder")) AddEditorObject(state, "folder", parentId);
        if (ImGui::MenuItem("Model")) AddEditorObject(state, "model", parentId);
        if (ImGui::BeginMenu("UI")) {
            if (ImGui::MenuItem("ScreenGui")) AddEditorObject(state, "screengui", parentId);
            if (ImGui::MenuItem("Frame")) AddEditorObject(state, "frame", parentId);
            if (ImGui::MenuItem("TextLabel")) AddEditorObject(state, "textlabel", parentId);
            if (ImGui::MenuItem("TextButton")) AddEditorObject(state, "textbutton", parentId);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Networking")) {
            if (ImGui::MenuItem("RemoteEvent")) AddEditorObject(state, "remoteevent", parentId);
            if (ImGui::MenuItem("RemoteFunction")) AddEditorObject(state, "remotefunction", parentId);
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void DrawEntityContextMenu(StudioState& state, const std::string& id) {
    if (!ImGui::BeginPopupContextItem("Object actions")) return;
    if (ImGui::MenuItem("Insert Script")) AddEditorObject(state, "script", id);
    if (ImGui::MenuItem("Insert LocalScript")) AddEditorObject(state, "localscript", id);
    ImGui::EndPopup();
}

void DrawEntityTree(StudioState& state, const std::string& parent) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
    for (std::size_t i = 0; i < state.map.entities.size(); ++i) {
        Entity& entity = state.map.entities[i];
        if (entity.parent != parent) continue;
        const std::string id = entity.id;
        const std::string name = entity.name;
        const std::string kind = entity.kind;
        const bool scriptObject = IsScriptObject(entity);
        ImGui::PushID(id.c_str());
        if (scriptObject) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                kind == "localscript" ? Rgb(0x55c7a5) : Rgb(0xe7ad62));
        }
        const bool hasChildren = std::any_of(state.map.entities.begin(), state.map.entities.end(),
            [&](const Entity& child) { return child.parent == id; });
        bool open = false;
        if (hasChildren) {
            open = ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_OpenOnArrow |
                (state.selectedEntity == static_cast<int>(i) ? ImGuiTreeNodeFlags_Selected : 0));
            if (state.selectedEntity == static_cast<int>(i)) DrawExplorerSelectionAccent();
            if (ImGui::IsItemClicked()) SelectHierarchyItem(state, i);
            if (scriptObject && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                SelectHierarchyItem(state, i);
                OpenScriptEditor(state, id);
            }
        } else {
            const float rowStart = ImGui::GetCursorScreenPos().x;
            const float rightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            const float plusWidth = ImGui::GetFrameHeight();
            const float labelWidth = std::max(20.0f, rightEdge - rowStart - plusWidth - 4.0f);
            if (ImGui::Selectable(name.c_str(), state.selectedEntity == static_cast<int>(i),
                                  ImGuiSelectableFlags_AllowDoubleClick, ImVec2(labelWidth, 0.0f))) {
                SelectHierarchyItem(state, i);
            }
            if (state.selectedEntity == static_cast<int>(i)) DrawExplorerSelectionAccent();
            if (scriptObject && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                SelectHierarchyItem(state, i);
                OpenScriptEditor(state, id);
            }
        }
        if (!scriptObject) DrawEntityContextMenu(state, id);
        DrawExplorerAddButton(state, id);
        if (scriptObject) ImGui::PopStyleColor();
        if (open) {
            DrawEntityTree(state, id);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar(2);
}

void DrawHierarchy(StudioState& state) {
    if (!ImGui::Begin("Explorer", &state.showExplorer)) { ImGui::End(); return; }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.5f));

    constexpr const char* services[] = {
        "Workspace", "ReplicatedStorage", "ServerScriptService", "ServerStorage", "StarterPlayer", "StarterGui",
    };

    for (std::size_t serviceIndex = 0; serviceIndex < std::size(services); ++serviceIndex) {
        const char* service = services[serviceIndex];
        ImGui::PushID(static_cast<int>(serviceIndex));
        ImGui::SetNextItemOpen(serviceIndex == 0, ImGuiCond_Once);
        const bool serviceOpen = ImGui::TreeNodeEx(service, ImGuiTreeNodeFlags_OpenOnArrow);
        if (state.selectedService == service) DrawExplorerSelectionAccent();
        if (ImGui::IsItemClicked()) {
            state.selectedEntity = -1;
            state.selectedService = service;
            state.showProperties = true;
        }
        DrawExplorerAddButton(state, service);
        if (serviceOpen) {
            DrawEntityTree(state, service);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar(2);
    ImGui::End();
}

void DrawInspector(StudioState& state) {
    if (!ImGui::Begin("Properties", &state.showProperties)) { ImGui::End(); return; }
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.56f);
    if (!state.selectedService.empty()) {
        const std::string service = state.selectedService;
        ImGui::TextDisabled("SERVICE  /  %s", service.c_str());
        ImGui::Separator();
        nlohmann::json& properties = state.map.serviceProperties[service];
        if (!properties.is_object()) properties = nlohmann::json::object();
        if (!properties.contains("archivable")) properties["archivable"] = true;
        bool archivable = properties.value("archivable", true);
        if (ImGui::Checkbox("Archivable", &archivable)) properties["archivable"] = archivable, state.dirty = true;
        if (service == "Workspace") {
            if (!properties.contains("gravity")) properties["gravity"] = 196.2f;
            float gravity = properties.value("gravity", 196.2f);
            if (ImGui::DragFloat("Gravity", &gravity, 0.5f, 0.0f, 1000.0f)) properties["gravity"] = gravity, state.dirty = true;
            bool streaming = properties.value("streamingEnabled", false);
            if (ImGui::Checkbox("Streaming enabled", &streaming)) properties["streamingEnabled"] = streaming, state.dirty = true;
        } else if (service == "StarterGui") {
            bool reset = properties.value("resetPlayerGuiOnSpawn", true);
            bool inset = properties.value("ignoreGuiInset", false);
            if (ImGui::Checkbox("Reset PlayerGui on spawn", &reset)) properties["resetPlayerGuiOnSpawn"] = reset, state.dirty = true;
            if (ImGui::Checkbox("Ignore GUI inset", &inset)) properties["ignoreGuiInset"] = inset, state.dirty = true;
            ImGui::TextWrapped("ScreenGui, Frame, TextLabel and TextButton objects under this service are saved with the map and previewed in the viewport.");
        } else if (service == "StarterPlayer") {
            float speed = properties.value("characterWalkSpeed", 16.0f);
            float jump = properties.value("characterJumpPower", 50.0f);
            if (ImGui::DragFloat("Character walk speed", &speed, 0.5f, 0.0f, 500.0f)) properties["characterWalkSpeed"] = speed, state.dirty = true;
            if (ImGui::DragFloat("Character jump power", &jump, 0.5f, 0.0f, 500.0f)) properties["characterJumpPower"] = jump, state.dirty = true;
        } else if (service == "ServerScriptService") {
            bool enabled = properties.value("enabled", true);
            if (ImGui::Checkbox("Enabled", &enabled)) properties["enabled"] = enabled, state.dirty = true;
        } else if (service == "ReplicatedStorage" || service == "ServerStorage") {
            ImGui::TextWrapped("Objects parented here are stored in the project hierarchy and map file.");
        }
        ImGui::PopItemWidth();
        ImGui::End();
        return;
    }
    if (state.selectedEntity < 0 || state.selectedEntity >= static_cast<int>(state.map.entities.size())) {
        ImGui::TextDisabled("Select an object in Explorer to edit it.");
        ImGui::PopItemWidth();
        ImGui::End();
        return;
    }
    Entity& entity = state.map.entities[static_cast<std::size_t>(state.selectedEntity)];
    ImGui::TextDisabled("%s  /  %s", IsScriptObject(entity) ? "SCRIPT" : "PART", entity.id.c_str());
    ImGui::Separator();
    char name[129]{};
    std::copy_n(entity.name.c_str(), std::min<std::size_t>(entity.name.size(), sizeof(name) - 1), name);
    if (ImGui::InputText("Name", name, sizeof(name))) entity.name = name, state.dirty = true;
    ImGui::Text("Class  %s", IsScriptObject(entity) ? (entity.kind == "localscript" ? "LocalScript" : "Script") : entity.kind.c_str());
    if (!IsScriptObject(entity)) {
        const std::string parentId = entity.id;
        if (ImGui::Button("Add Script")) {
            AddEditorObject(state, "script", parentId);
            ImGui::PopItemWidth();
            ImGui::End();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add LocalScript")) {
            AddEditorObject(state, "localscript", parentId);
            ImGui::PopItemWidth();
            ImGui::End();
            return;
        }
    }
    if (IsScriptObject(entity)) {
        ImGui::Text("Parent  %s", entity.parent.c_str());
        ImGui::TextWrapped("Scripts are saved with this .blubox map. Use the Luau Script panel to edit and preview the source.");
    } else {
        static blubox::engine::reflection::ReflectionRegistry reflection;
        static bool reflectionReady = false;
        if (!reflectionReady) {
            blubox::engine::reflection::RegisterBuiltinReflection(reflection);
            reflectionReady = true;
        }
        static blubox::engine::editor::PropertiesPanel propertiesPanel(reflection);
        if (propertiesPanel.draw(entity) != blubox::engine::reflection::ChangeNone) state.dirty = true;
        if ((entity.kind == "block" || entity.kind == "meshpart") && ImGui::Button("Choose albedo texture", ImVec2(-1, 0)))
            ChooseAlbedoForEntity(state, entity);
        if ((entity.kind == "block" || entity.kind == "meshpart") && ImGui::Button("Add decal", ImVec2(-1, 0))) {
            AddEditorObject(state, "decal", entity.id);
            ImGui::End();
            ImGui::PopItemWidth();
            return;
        }
        if (entity.kind == "decal" && ImGui::Button("Choose decal image", ImVec2(-1, 0))) ChooseTextureForDecal(state, entity);
        if (entity.kind == "screengui")
            ImGui::TextWrapped("Frames, TextLabels and TextButtons under this ScreenGui are rendered in the viewport preview.");
    }
    ImGui::Spacing();
    if (ImGui::Button(IsScriptObject(entity) ? "Delete script" : "Delete object", ImVec2(-1, 0))) {
        const std::string deletedId = entity.id;
        const std::string deletedName = entity.name;
        std::erase_if(state.map.entities, [&](const Entity& candidate) {
            return candidate.id == deletedId || candidate.parent == deletedId;
        });
        std::erase(state.openScriptIds, deletedId);
        state.scriptEditors.erase(deletedId);
        state.selectedEntity = -1;
        state.selectedService.clear();
        state.dirty = true;
        AppendOutput(state, "INFO", "Deleted " + deletedName + ".");
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

void RefreshCompletionWorkspaceChildren(StudioState& state) {
    const auto now = std::chrono::steady_clock::now();
    if (now < state.nextWorkspaceCompletionRefresh) return;
    state.nextWorkspaceCompletionRefresh = now + std::chrono::milliseconds(350);
    std::vector<blubox::engine::intellisense::WorkspaceChild> children;
    children.reserve(state.map.entities.size());
    std::size_t hash = 1469598103934665603ull;
    const auto mix = [&](std::string_view value) {
        for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ull; }
        hash ^= 0xff; hash *= 1099511628211ull;
    };
    for (const Entity& entity : state.map.entities) {
        std::string parent = entity.parent;
        std::transform(parent.begin(), parent.end(), parent.begin(), [](unsigned char c) {
            return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        });
        if (!parent.empty() && parent != "workspace") continue;
        if (entity.name.empty()) continue;
        std::string type = "Instance";
        if (entity.kind == "block" || entity.kind == "part" || entity.kind == "spawnlocation" || entity.kind == "meshpart") type = entity.kind == "spawnlocation" ? "SpawnLocation" : "Part";
        else if (entity.kind == "model") type = "Model";
        else if (entity.kind == "folder") type = "Folder";
        else if (entity.kind == "script") type = "Script";
        else if (entity.kind == "localscript") type = "LocalScript";
        else if (entity.kind == "screengui") type = "ScreenGui";
        else if (entity.kind == "frame") type = "Frame";
        else if (entity.kind == "textlabel") type = "TextLabel";
        else if (entity.kind == "textbutton") type = "TextButton";
        children.push_back({entity.name, {}, std::move(type)});
        mix(entity.id);
        mix(entity.name);
        mix(entity.kind);
        mix(entity.parent);
    }
    if (hash == state.workspaceCompletionHash) return;
    state.workspaceCompletionHash = hash;
    state.completionEngine.setWorkspaceChildren(std::move(children));
}

bool DrawCompletionPopup(StudioState& state, TextEditor& editor, const std::string& scriptId,
                         Entity& entity, bool editorFocused) {
    auto& response = state.completion;
    if (!editorFocused || state.completionForScript != scriptId || !response.active || response.items.empty())
        return false;
    state.completionSelection = std::clamp(
        state.completionSelection, 0, static_cast<int>(response.items.size()) - 1);

    constexpr float rowHeight = 23.0f;
    constexpr float width = 350.0f;
    const int visibleRows = std::min(8, static_cast<int>(response.items.size()));
    const float height = 29.0f + visibleRows * rowHeight + 52.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workMin = viewport->WorkPos;
    const ImVec2 workMax(workMin.x + viewport->WorkSize.x, workMin.y + viewport->WorkSize.y);
    const ImVec2 caret = editor.GetCursorScreenPosition();
    const float lineHeight = std::max(16.0f, editor.GetCursorLineHeight());
    const float popupWidth = std::min(width, std::max(240.0f, workMax.x - workMin.x - 12.0f));
    const float maxX = std::max(workMin.x + 6.0f, workMax.x - popupWidth - 6.0f);
    const float x = std::clamp(caret.x, workMin.x + 6.0f, maxX);
    float y = caret.y + lineHeight + 4.0f;
    if (y + height > workMax.y - 4.0f) y = caret.y - height - 4.0f;
    y = std::clamp(y, workMin.y + 4.0f, std::max(workMin.y + 4.0f, workMax.y - height - 4.0f));
    const ImVec2 popupMin(x, y), popupMax(x + popupWidth, y + height);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImU32 background = ImGui::ColorConvertFloat4ToU32(ThemeColor(blubox::engine::ui::kDarkTheme.container));
    const ImU32 outline = ImGui::ColorConvertFloat4ToU32(ThemeColor(blubox::engine::ui::kDarkTheme.borderSubtle));
    const ImU32 primary = ImGui::ColorConvertFloat4ToU32(ThemeColor(blubox::engine::ui::kDarkTheme.textPrimary));
    const ImU32 muted = ImGui::ColorConvertFloat4ToU32(ThemeColor(blubox::engine::ui::kDarkTheme.textMuted));
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(ThemeColor(blubox::engine::ui::kDarkTheme.accent));
    const ImU32 selectedBg = ImGui::ColorConvertFloat4ToU32(ThemeColor(blubox::engine::ui::kDarkTheme.selected));
    draw->AddRectFilled(popupMin, popupMax, background, 6.0f);
    draw->AddRect(popupMin, popupMax, outline, 6.0f);
    draw->PushClipRect(popupMin, popupMax, true);

    const std::string header = response.receiverType.empty() ? "Luau suggestions" : response.receiverType;
    draw->AddText(ImVec2(x + 10.0f, y + 6.0f), muted, header.c_str());
    const float separatorY = y + 26.0f;
    draw->AddLine(ImVec2(x + 7.0f, separatorY), ImVec2(x + popupWidth - 7.0f, separatorY), outline);

    if (state.completionSelection < state.completionScroll) state.completionScroll = state.completionSelection;
    if (state.completionSelection >= state.completionScroll + visibleRows)
        state.completionScroll = state.completionSelection - visibleRows + 1;
    state.completionScroll = std::clamp(state.completionScroll, 0,
        std::max(0, static_cast<int>(response.items.size()) - visibleRows));

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const auto inside = [](ImVec2 p, ImVec2 min, ImVec2 max) {
        return p.x >= min.x && p.y >= min.y && p.x < max.x && p.y < max.y;
    };
    bool accepted = false;
    const float listTop = y + 29.0f;
    for (int row = 0; row < visibleRows; ++row) {
        const int index = state.completionScroll + row;
        const auto& item = response.items[static_cast<std::size_t>(index)];
        const float rowY = listTop + row * rowHeight;
        const ImVec2 rowMin(x + 5.0f, rowY), rowMax(x + popupWidth - 5.0f, rowY + rowHeight);
        if (ImGui::IsMousePosValid() && inside(mouse, rowMin, rowMax)) {
            state.completionSelection = index;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) accepted = true;
        }
        if (state.completionSelection == index) draw->AddRectFilled(rowMin, rowMax, selectedBg, 3.0f);
        draw->AddCircleFilled(ImVec2(x + 15.0f, rowY + rowHeight * 0.5f), 3.0f,
                              state.completionSelection == index ? accent : muted);
        draw->AddText(ImVec2(x + 25.0f, rowY + 4.0f), primary, item.label.c_str());
        const ImVec2 kindSize = ImGui::CalcTextSize(item.kind.c_str());
        draw->AddText(ImVec2(x + popupWidth - kindSize.x - 12.0f, rowY + 4.0f), muted, item.kind.c_str());
    }

    const float detailTop = listTop + visibleRows * rowHeight;
    draw->AddLine(ImVec2(x + 7.0f, detailTop), ImVec2(x + popupWidth - 7.0f, detailTop), outline);
    const auto& selected = response.items[static_cast<std::size_t>(std::clamp(
        state.completionSelection, 0, static_cast<int>(response.items.size()) - 1))];
    draw->AddText(ImVec2(x + 10.0f, detailTop + 6.0f), accent, selected.label.c_str());
    draw->AddText(ImVec2(x + 10.0f, detailTop + 25.0f), muted, selected.detail.c_str());
    draw->PopClipRect();

    if (editorFocused) {
        const int navigation = editor.TakeCompletionNavigationRequest();
        state.completionSelection = std::clamp(state.completionSelection + navigation, 0,
            static_cast<int>(response.items.size()) - 1);
        accepted = editor.TakeCompletionAcceptRequest();
    }
    if (editorFocused && editor.TakeCompletionDismissRequest()) {
        state.completionController.clear();
        state.completion = {};
        state.completionForScript.clear();
        editor.SetCompletionPopupActive(false);
        return false;
    }
    if (inside(mouse, popupMin, popupMax) && ImGui::GetIO().MouseWheel != 0.0f)
        state.completionScroll = std::clamp(state.completionScroll + (ImGui::GetIO().MouseWheel < 0.0f ? 1 : -1),
            0, std::max(0, static_cast<int>(response.items.size()) - visibleRows));
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inside(mouse, popupMin, popupMax)) {
        state.completionController.clear();
        state.completion = {};
        state.completionForScript.clear();
        editor.SetCompletionPopupActive(false);
        return false;
    }

    if (!accepted) return false;
    const auto item = response.items[static_cast<std::size_t>(std::clamp(
        state.completionSelection, 0, static_cast<int>(response.items.size()) - 1))];
    const TextEditor::Coordinates caretPosition = editor.GetCursorPosition();
    const int queryLength = std::min(static_cast<int>(response.query.size()), caretPosition.mColumn);
    const TextEditor::Coordinates prefixStart(caretPosition.mLine, caretPosition.mColumn - queryLength);
    // Replace only the identifier prefix immediately before the caret. Using
    // the current caret also protects the rest of the line when the completion
    // snapshot's range is stale or the editor previously held a selection.
    editor.ReplaceTextRange(prefixStart, caretPosition, item.insertion);
    entity.luauScript = editor.GetText();
    state.dirty = true;
    state.dirtyScriptIds.insert(scriptId);
    state.completionController.clear();
    state.completion = {};
    state.completionForScript.clear();
    editor.SetCompletionPopupActive(false);
    return true;
}

void DrawScriptPanel(StudioState& state) {
    // Called once for every open script document. Dock tabs are the document
    // tabs, so each script shares the main workspace with the Viewport.
    RefreshCompletionWorkspaceChildren(state);
    const std::vector<std::string> openIds = state.openScriptIds;
    for (const std::string& id : openIds) {
        auto entity = std::find_if(state.map.entities.begin(), state.map.entities.end(),
            [&](const Entity& item) { return item.id == id; });
        if (entity == state.map.entities.end() || !IsScriptObject(*entity)) {
            std::erase(state.openScriptIds, id);
            state.scriptEditors.erase(id);
            continue;
        }

        auto& editor = state.scriptEditors[id];
        if (!editor) {
            editor = std::make_unique<TextEditor>();
            editor->SetLanguageDefinition(LuauLanguage());
            editor->SetPalette(LuauEditorPalette());
            editor->SetTabSize(4);
            editor->SetText(entity->luauScript);
        }

        const std::string type = entity->kind == "localscript" ? "LocalScript" : "Script";
        const std::string title = type + " - " + entity->name + "###ScriptEditor_" + id;
        bool open = true;
        if (state.centralDockNode != 0) ImGui::SetNextWindowDockID(state.centralDockNode, ImGuiCond_FirstUseEver);
        if (state.focusScriptId == id) {
            ImGui::SetNextWindowFocus();
            state.focusScriptId.clear();
        }
        const bool visible = ImGui::Begin(title.c_str(), &open);
        if (visible) {
            const auto savedSource = state.scriptSavedSource.find(id);
            const bool scriptDirty = savedSource == state.scriptSavedSource.end() || state.dirtyScriptIds.contains(id);
            ImGui::TextColored(scriptDirty ? Rgb(0xe7ad62) : (entity->kind == "localscript" ? Rgb(0x55c7a5) : Rgb(0x7a8597)),
                "%s", "●");
            ImGui::SameLine();
            ImGui::TextUnformatted(entity->name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s  ·  %zu bytes", entity->parent.c_str(), entity->luauScript.size());
            ImGui::Separator();
            float& zoom = state.scriptZoom[id];
            if (zoom == 0.0f) zoom = 1.0f;
            const bool control = ImGui::GetIO().KeyCtrl;
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && control) {
                if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) zoom = std::min(2.5f, zoom + 0.1f);
                if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) zoom = std::max(0.6f, zoom - 0.1f);
                if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) zoom = 1.0f;
            }
            const ImVec2 editorSize(0.0f, std::max(120.0f, ImGui::GetContentRegionAvail().y));
            editor->SetCompletionPopupActive(state.completionForScript == id && state.completion.active);
            ImFont* activeCodeFont = CodeFontForZoom(zoom);
            if (activeCodeFont) ImGui::PushFont(activeCodeFont);
            editor->Render("##LuauSource", editorSize, false);
            if (activeCodeFont) ImGui::PopFont();
            const float wheelZoomDelta = editor->TakeWheelZoomDelta();
            if (wheelZoomDelta != 0.0f)
                zoom = std::clamp(zoom + wheelZoomDelta * 0.1f, 0.6f, 2.5f);

            const auto cursor = editor->GetCursorPosition();
            const bool textChanged = editor->IsTextChanged();
            const bool cursorChanged = editor->IsCursorPositionChanged();
            if (textChanged) {
                entity->luauScript = editor->GetText();
                state.dirty = true;
                state.dirtyScriptIds.insert(id);
                state.completionController.onTextChanged({
                    blubox::engine::editor::TextChangeReason::Replace,
                    cursor.mLine, cursor.mColumn, 0, {}});
                state.completionForScript = id;
                state.completionSelection = 0;
                state.completionScroll = 0;
            }
            if (cursorChanged)
                state.completionController.onCaretMoved(cursor.mLine, cursor.mColumn);

            const bool editorFocused = editor->IsFocused();
            const bool explicitInvocation = editorFocused && control && ImGui::IsKeyPressed(ImGuiKey_Space, false);
            if (explicitInvocation) {
                state.completionForScript = id;
                state.completionSelection = 0;
                state.completionScroll = 0;
            }
            if (editorFocused && (textChanged || cursorChanged || explicitInvocation))
                state.completionForScript = id;
            if (editorFocused && (explicitInvocation || state.completionController.needsUpdate(state.completionEngine, id))) {
                state.completionController.update(state.completionEngine, id,
                    editor->GetLineText(cursor.mLine), cursor.mLine, cursor.mColumn,
                    true, explicitInvocation);
                state.completion = state.completionController.response();
            } else if (!editorFocused && state.completionForScript == id) {
                state.completionController.update(state.completionEngine, id,
                    editor->GetLineText(cursor.mLine), cursor.mLine, cursor.mColumn, false);
                state.completion = {};
                state.completionForScript.clear();
            }

            std::string refreshScriptId;
            std::uint64_t refreshGeneration = 0;
            if (state.completionForScript == id && state.completionController.takeSemanticRefreshRequest(
                    std::chrono::steady_clock::now(), refreshScriptId, refreshGeneration)) {
                state.completionEngine.requestSemanticRefresh(refreshScriptId, refreshGeneration, editor->GetText());
            }
            editor->SetCompletionPopupActive(state.completionForScript == id && state.completion.active);
            DrawCompletionPopup(state, *editor, id, *entity, editorFocused);
            ImGui::Separator();
            ImGui::TextDisabled("Luau  ·  %d lines", editor->GetTotalLines());
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 120.0f));
            ImGui::TextDisabled("Ln %d, Col %d", cursor.mLine + 1, cursor.mColumn + 1);
            ImGui::SameLine(); ImGui::TextDisabled("Ctrl+Wheel: %.0f%%", zoom * 100.0f);
        }
        ImGui::End();
        if (!open) {
            std::erase(state.openScriptIds, id);
            state.scriptEditors.erase(id);
        }
    }
}

std::array<float, 16> ToColumnMajor(const Matrix& matrix) {
    return {matrix.m0, matrix.m1, matrix.m2, matrix.m3,
            matrix.m4, matrix.m5, matrix.m6, matrix.m7,
            matrix.m8, matrix.m9, matrix.m10, matrix.m11,
            matrix.m12, matrix.m13, matrix.m14, matrix.m15};
}

Vector3 NormalizeVector(Vector3 value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.00001f) return Vector3{0.0f, 0.0f, 0.0f};
    return Vector3{value.x / length, value.y / length, value.z / length};
}

Vector3 RotateVectorX(Vector3 value, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return Vector3{value.x, value.y * c - value.z * s, value.y * s + value.z * c};
}

Vector3 RotateVectorY(Vector3 value, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return Vector3{value.x * c + value.z * s, value.y, -value.x * s + value.z * c};
}

Vector3 RotateVectorZ(Vector3 value, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return Vector3{value.x * c - value.y * s, value.x * s + value.y * c, value.z};
}

bool IntersectEntity(const Entity& entity, Vector3 rayOrigin, Vector3 rayDirection, float& distance) {
    const auto& transform = entity.transform;
    Vector3 localOrigin{rayOrigin.x - transform.position[0], rayOrigin.y - transform.position[1], rayOrigin.z - transform.position[2]};
    Vector3 localDirection = rayDirection;
    localOrigin = RotateVectorX(localOrigin, -transform.rotation[0]);
    localOrigin = RotateVectorY(localOrigin, -transform.rotation[1]);
    localOrigin = RotateVectorZ(localOrigin, -transform.rotation[2]);
    localDirection = RotateVectorX(localDirection, -transform.rotation[0]);
    localDirection = RotateVectorY(localDirection, -transform.rotation[1]);
    localDirection = RotateVectorZ(localDirection, -transform.rotation[2]);

    if (entity.kind == "sphere") {
        const float radius = transform.scale[0] * 0.5f;
        const float b = localOrigin.x * localDirection.x + localOrigin.y * localDirection.y + localOrigin.z * localDirection.z;
        const float c = localOrigin.x * localOrigin.x + localOrigin.y * localOrigin.y + localOrigin.z * localOrigin.z - radius * radius;
        const float discriminant = b * b - c;
        if (discriminant < 0.0f) return false;
        const float nearHit = -b - std::sqrt(discriminant);
        const float farHit = -b + std::sqrt(discriminant);
        distance = nearHit >= 0.0f ? nearHit : farHit;
        return distance >= 0.0f;
    }

    const float halfSize[3] = {transform.scale[0] * 0.5f, transform.scale[1] * 0.5f, transform.scale[2] * 0.5f};
    const float origin[3] = {localOrigin.x, localOrigin.y, localOrigin.z};
    const float direction[3] = {localDirection.x, localDirection.y, localDirection.z};
    float nearHit = 0.0f;
    float farHit = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 0.00001f) {
            if (origin[axis] < -halfSize[axis] || origin[axis] > halfSize[axis]) return false;
            continue;
        }
        float first = (-halfSize[axis] - origin[axis]) / direction[axis];
        float second = (halfSize[axis] - origin[axis]) / direction[axis];
        if (first > second) std::swap(first, second);
        nearHit = std::max(nearHit, first);
        farHit = std::min(farHit, second);
        if (nearHit > farHit) return false;
    }
    distance = nearHit;
    return farHit >= 0.0f;
}

int PickEntity(const StudioState& state, float screenX, float screenY, float width, float height) {
    if (width <= 0.0f || height <= 0.0f) return -1;
    const float u = std::clamp(screenX / width, 0.0f, 1.0f);
    const float v = std::clamp(screenY / height, 0.0f, 1.0f);
    const float tangent = std::tan(state.camera.fovy * DEG2RAD * 0.5f);
    const Vector3 forward = NormalizeVector(Vector3{
        state.camera.target.x - state.camera.position.x,
        state.camera.target.y - state.camera.position.y,
        state.camera.target.z - state.camera.position.z});
    const Vector3 right = NormalizeVector(Vector3{
        forward.y * state.camera.up.z - forward.z * state.camera.up.y,
        forward.z * state.camera.up.x - forward.x * state.camera.up.z,
        forward.x * state.camera.up.y - forward.y * state.camera.up.x});
    const Vector3 up = NormalizeVector(Vector3{
        right.y * forward.z - right.z * forward.y,
        right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x});
    const float ndcX = (2.0f * u - 1.0f) * (width / height) * tangent;
    const float ndcY = (1.0f - 2.0f * v) * tangent;
    const Vector3 rayDirection = NormalizeVector(Vector3{
        forward.x + right.x * ndcX + up.x * ndcY,
        forward.y + right.y * ndcX + up.y * ndcY,
        forward.z + right.z * ndcX + up.z * ndcY});

    int picked = -1;
    float closest = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < state.map.entities.size(); ++i) {
        const Entity& entity = state.map.entities[i];
        if (!entity.visible || (entity.kind != "block" && entity.kind != "sphere" && entity.kind != "spawnlocation") || entity.parent != "Workspace") continue;
        float hitDistance = 0.0f;
        if (IntersectEntity(entity, state.camera.position, rayDirection, hitDistance) && hitDistance < closest) {
            closest = hitDistance;
            picked = static_cast<int>(i);
        }
    }
    return picked;
}

void UpdateViewportCamera(StudioState& state, float deltaTime) {
    if (!state.cameraAnglesInitialized) {
        const Vector3 direction = NormalizeVector(Vector3{
            state.camera.target.x - state.camera.position.x,
            state.camera.target.y - state.camera.position.y,
            state.camera.target.z - state.camera.position.z});
        state.cameraYaw = std::atan2(direction.z, direction.x);
        state.cameraPitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
        state.cameraAnglesInitialized = true;
    }

    const bool wantLook = state.viewportHovered && IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    if (wantLook && !state.cameraLookCaptured) {
        state.cameraLookCaptured = true;
        state.viewportFocused = true;
        DisableCursor();
    } else if (!wantLook && state.cameraLookCaptured) {
        state.cameraLookCaptured = false;
        EnableCursor();
    }

    if (!state.viewportFocused || ImGui::GetIO().WantTextInput) return;
    if (!state.cameraLookCaptured) {
        if (IsKeyPressed(KEY_W)) state.gizmoOperation = ImGuizmo::TRANSLATE, state.gizmoEnabled = true;
        if (IsKeyPressed(KEY_E)) state.gizmoOperation = ImGuizmo::ROTATE, state.gizmoEnabled = true;
        if (IsKeyPressed(KEY_R)) state.gizmoOperation = ImGuizmo::SCALE, state.gizmoEnabled = true;
        return;
    }
    const float speed = IsKeyDown(KEY_LEFT_SHIFT) ? 18.0f : 7.0f;
    const float step = speed * std::clamp(deltaTime, 0.0f, 0.05f);
    Vector3 forward{std::cos(state.cameraPitch) * std::cos(state.cameraYaw),
                    std::sin(state.cameraPitch),
                    std::cos(state.cameraPitch) * std::sin(state.cameraYaw)};
    if (state.cameraLookCaptured) {
        const Vector2 mouse = GetMouseDelta();
        state.cameraYaw += mouse.x * 0.003f;
        state.cameraPitch = std::clamp(state.cameraPitch - mouse.y * 0.003f, -1.52f, 1.52f);
        forward = Vector3{std::cos(state.cameraPitch) * std::cos(state.cameraYaw),
                          std::sin(state.cameraPitch),
                          std::cos(state.cameraPitch) * std::sin(state.cameraYaw)};
    }
    const Vector3 horizontal = NormalizeVector(Vector3{forward.x, 0.0f, forward.z});
    const Vector3 right{-horizontal.z, 0.0f, horizontal.x};
    Vector3 movement{0.0f, 0.0f, 0.0f};
    if (IsKeyDown(KEY_W)) movement = Vector3Add(movement, forward);
    if (IsKeyDown(KEY_S)) movement = Vector3Subtract(movement, forward);
    if (IsKeyDown(KEY_D)) movement = Vector3Add(movement, right);
    if (IsKeyDown(KEY_A)) movement = Vector3Subtract(movement, right);
    if (IsKeyDown(KEY_SPACE)) movement.y += 1.0f;
    if (IsKeyDown(KEY_LEFT_CONTROL)) movement.y -= 1.0f;
    if (movement.x != 0.0f || movement.y != 0.0f || movement.z != 0.0f) {
        movement = Vector3Scale(NormalizeVector(movement), step);
        state.camera.position = Vector3Add(state.camera.position, movement);
    }
    state.camera.target = Vector3Add(state.camera.position, forward);
}

bool IsGuiElement(const Entity& entity) {
    return entity.kind == "frame" || entity.kind == "textlabel" || entity.kind == "textbutton";
}

float GuiVectorComponent(const nlohmann::json& value, std::size_t component, float fallback) {
    if (!value.is_array() || value.size() <= component || !value[component].is_number()) return fallback;
    return value[component].get<float>();
}

std::optional<GuiElementRect> GetGuiElementRect(
    const Entity& entity, const std::unordered_map<std::string, const Entity*>& byId,
    float viewportWidth, float viewportHeight, std::size_t depth) {
    if (!IsGuiElement(entity) || !entity.properties.value("visible", true)) return std::nullopt;
    if (depth >= 64) return std::nullopt;
    GuiElementRect parentRect{0.0f, 0.0f, std::max(1.0f, viewportWidth), std::max(1.0f, viewportHeight)};
    const auto parentIt = byId.find(entity.parent);
    if (parentIt == byId.end()) return std::nullopt;
    const Entity& parent = *parentIt->second;
    if (parent.kind == "screengui") {
        if (parent.parent != "StarterGui" || !parent.properties.value("enabled", true)) return std::nullopt;
    } else {
        if (!IsGuiElement(parent)) return std::nullopt;
        auto resolvedParent = GetGuiElementRect(parent, byId, viewportWidth, viewportHeight, depth + 1);
        if (!resolvedParent) return std::nullopt;
        parentRect = *resolvedParent;
    }

    const bool scaleWithViewport = entity.properties.value("scaleWithViewport", true);
    const float scaleX = scaleWithViewport ? viewportWidth / 1280.0f : 1.0f;
    const float scaleY = scaleWithViewport ? viewportHeight / 720.0f : 1.0f;
    const auto position = entity.properties.value("position", nlohmann::json::array({40, 40}));
    const auto size = entity.properties.value("size", nlohmann::json::array({220, 64}));
    const auto positionScale = entity.properties.value("positionScale", nlohmann::json::array({0.0f, 0.0f}));
    const auto sizeScale = entity.properties.value("sizeScale", nlohmann::json::array({0.0f, 0.0f}));
    const float width = std::clamp(GuiVectorComponent(size, 0, 220.0f), 8.0f, 100000.0f) * scaleX +
        std::clamp(GuiVectorComponent(sizeScale, 0, 0.0f), 0.0f, 10.0f) * parentRect.width;
    const float height = std::clamp(GuiVectorComponent(size, 1, 64.0f), 8.0f, 100000.0f) * scaleY +
        std::clamp(GuiVectorComponent(sizeScale, 1, 0.0f), 0.0f, 10.0f) * parentRect.height;
    float x = std::clamp(GuiVectorComponent(position, 0, 40.0f), -100000.0f, 100000.0f) * scaleX +
        std::clamp(GuiVectorComponent(positionScale, 0, 0.0f), -10.0f, 10.0f) * parentRect.width;
    float y = std::clamp(GuiVectorComponent(position, 1, 40.0f), -100000.0f, 100000.0f) * scaleY +
        std::clamp(GuiVectorComponent(positionScale, 1, 0.0f), -10.0f, 10.0f) * parentRect.height;
    const float resolvedWidth = std::clamp(width, 8.0f, 400000.0f);
    const float resolvedHeight = std::clamp(height, 8.0f, 400000.0f);
    const std::string horizontal = entity.properties.value("horizontalAlignment", std::string("Left"));
    const std::string vertical = entity.properties.value("verticalAlignment", std::string("Top"));
    if (horizontal == "Center") x += (parentRect.width - resolvedWidth) * 0.5f;
    else if (horizontal == "Right") x += parentRect.width - resolvedWidth;
    if (vertical == "Center") y += (parentRect.height - resolvedHeight) * 0.5f;
    else if (vertical == "Bottom") y += parentRect.height - resolvedHeight;
    return GuiElementRect{std::clamp(parentRect.x + x, -400000.0f, 400000.0f),
                          std::clamp(parentRect.y + y, -400000.0f, 400000.0f),
                          resolvedWidth, resolvedHeight};
}

std::array<ImVec2, 8> GuiResizeHandleCenters(const GuiElementRect& rect, const ImVec2& viewportOrigin) {
    const float left = viewportOrigin.x + rect.x;
    const float top = viewportOrigin.y + rect.y;
    const float right = left + rect.width;
    const float bottom = top + rect.height;
    const float centerX = (left + right) * 0.5f;
    const float centerY = (top + bottom) * 0.5f;
    return {ImVec2(left, top), ImVec2(centerX, top), ImVec2(right, top), ImVec2(right, centerY),
            ImVec2(right, bottom), ImVec2(centerX, bottom), ImVec2(left, bottom), ImVec2(left, centerY)};
}

int PickGuiElement(const MapDocument& map, float x, float y, float viewportWidth, float viewportHeight) {
    std::unordered_map<std::string, const Entity*> byId;
    byId.reserve(map.entities.size());
    for (const Entity& entity : map.entities) byId.emplace(entity.id, &entity);
    for (std::size_t i = map.entities.size(); i > 0; --i) {
        const Entity& entity = map.entities[i - 1];
        const auto rect = GetGuiElementRect(entity, byId, viewportWidth, viewportHeight);
        if (rect && x >= rect->x && y >= rect->y && x <= rect->x + rect->width && y <= rect->y + rect->height)
            return static_cast<int>(i - 1);
    }
    return -1;
}

bool HandleGuiViewportSelection(StudioState& state, const ImVec2& viewportOrigin) {
    const auto isEditableMode = [&] {
        if (!state.playMode) return true;
        const auto playState = state.playMode->state();
        return playState == blubox::engine::play::PlayModeState::Stopped ||
               playState == blubox::engine::play::PlayModeState::Failed;
    };
    if (!isEditableMode()) {
        state.guiDragActive = false;
        return false;
    }

    bool handledClick = false;
    const float viewportWidth = static_cast<float>(std::max(1, state.viewport.texture.width));
    const float viewportHeight = static_cast<float>(std::max(1, state.viewport.texture.height));
    const float scaleX = viewportWidth / 1280.0f;
    const float scaleY = viewportHeight / 720.0f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float localX = mouse.x - viewportOrigin.x;
    const float localY = mouse.y - viewportOrigin.y;
    Entity* selected = state.selectedEntity >= 0 && state.selectedEntity < static_cast<int>(state.map.entities.size())
        ? &state.map.entities[static_cast<std::size_t>(state.selectedEntity)] : nullptr;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && state.viewportHovered) {
        // Check the selected element's grips first, since they sit just outside
        // its rectangle and therefore are not part of ordinary hit testing.
        if (selected && IsGuiElement(*selected)) {
            std::unordered_map<std::string, const Entity*> byId;
            byId.reserve(state.map.entities.size());
            for (const Entity& item : state.map.entities) byId.emplace(item.id, &item);
            const auto rect = GetGuiElementRect(*selected, byId, viewportWidth, viewportHeight);
            if (rect) {
                const auto centers = GuiResizeHandleCenters(*rect, viewportOrigin);
                for (int i = 0; i < static_cast<int>(centers.size()); ++i) {
                    if (std::abs(mouse.x - centers[static_cast<std::size_t>(i)].x) <= 7.0f &&
                        std::abs(mouse.y - centers[static_cast<std::size_t>(i)].y) <= 7.0f) {
                        state.guiDragActive = true;
                        state.guiDragHandle = i;
                        state.guiDragEntityId = selected->id;
                        state.guiDragStartMouse = mouse;
                        const auto p = selected->properties.value("position", nlohmann::json::array({40, 40}));
                        const auto s = selected->properties.value("size", nlohmann::json::array({220, 64}));
                        state.guiDragStartPosition = {static_cast<int>(GuiVectorComponent(p, 0, 40.0f)),
                                                      static_cast<int>(GuiVectorComponent(p, 1, 40.0f))};
                        state.guiDragStartSize = {static_cast<int>(GuiVectorComponent(s, 0, 220.0f)),
                                                  static_cast<int>(GuiVectorComponent(s, 1, 64.0f))};
                        handledClick = true;
                        break;
                    }
                }
            }
        }
        if (!handledClick) {
            const int picked = PickGuiElement(state.map, localX, localY, viewportWidth, viewportHeight);
            if (picked >= 0) {
                const bool alreadySelected = state.selectedEntity == picked;
                SelectHierarchyItem(state, static_cast<std::size_t>(picked));
                selected = &state.map.entities[static_cast<std::size_t>(picked)];
                handledClick = true;
                if (alreadySelected) {
                    state.guiDragActive = true;
                    state.guiDragHandle = -1;
                    state.guiDragEntityId = selected->id;
                    state.guiDragStartMouse = mouse;
                    const auto p = selected->properties.value("position", nlohmann::json::array({40, 40}));
                    const auto s = selected->properties.value("size", nlohmann::json::array({220, 64}));
                    state.guiDragStartPosition = {static_cast<int>(GuiVectorComponent(p, 0, 40.0f)),
                                                  static_cast<int>(GuiVectorComponent(p, 1, 40.0f))};
                    state.guiDragStartSize = {static_cast<int>(GuiVectorComponent(s, 0, 220.0f)),
                                              static_cast<int>(GuiVectorComponent(s, 1, 64.0f))};
                }
            } else {
                state.guiDragActive = false;
            }
        }
    }

    if (state.guiDragActive) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            state.guiDragActive = false;
        } else {
            const auto found = std::find_if(state.map.entities.begin(), state.map.entities.end(), [&](const Entity& item) {
                return item.id == state.guiDragEntityId;
            });
            if (found == state.map.entities.end() || !IsGuiElement(*found)) {
                state.guiDragActive = false;
            } else {
                const float responsiveX = found->properties.value("scaleWithViewport", true) ? scaleX : 1.0f;
                const float responsiveY = found->properties.value("scaleWithViewport", true) ? scaleY : 1.0f;
                const float dx = (mouse.x - state.guiDragStartMouse.x) / std::max(0.01f, responsiveX);
                const float dy = (mouse.y - state.guiDragStartMouse.y) / std::max(0.01f, responsiveY);
                float left = static_cast<float>(state.guiDragStartPosition[0]);
                float top = static_cast<float>(state.guiDragStartPosition[1]);
                float right = left + static_cast<float>(state.guiDragStartSize[0]);
                float bottom = top + static_cast<float>(state.guiDragStartSize[1]);
                if (state.guiDragHandle == -1) {
                    left += dx;
                    right += dx;
                    top += dy;
                    bottom += dy;
                } else {
                    const int handle = state.guiDragHandle;
                    if (handle == 0 || handle == 6 || handle == 7) left += dx;
                    if (handle == 2 || handle == 3 || handle == 4) right += dx;
                    if (handle == 0 || handle == 1 || handle == 2) top += dy;
                    if (handle == 4 || handle == 5 || handle == 6) bottom += dy;
                    if (right - left < 8.0f) {
                        if (handle == 0 || handle == 6 || handle == 7) left = right - 8.0f;
                        else right = left + 8.0f;
                    }
                    if (bottom - top < 8.0f) {
                        if (handle == 0 || handle == 1 || handle == 2) top = bottom - 8.0f;
                        else bottom = top + 8.0f;
                    }
                }
                const int x = static_cast<int>(std::lround(std::clamp(left, -100000.0f, 100000.0f)));
                const int y = static_cast<int>(std::lround(std::clamp(top, -100000.0f, 100000.0f)));
                const int width = static_cast<int>(std::lround(std::clamp(right - left, 8.0f, 100000.0f)));
                const int height = static_cast<int>(std::lround(std::clamp(bottom - top, 8.0f, 100000.0f)));
                found->properties["position"] = {x, y};
                found->properties["size"] = {width, height};
                state.dirty = true;
            }
        }
    }

    if (state.selectedEntity >= 0 && state.selectedEntity < static_cast<int>(state.map.entities.size())) {
        selected = &state.map.entities[static_cast<std::size_t>(state.selectedEntity)];
        if (IsGuiElement(*selected)) {
            std::unordered_map<std::string, const Entity*> byId;
            byId.reserve(state.map.entities.size());
            for (const Entity& item : state.map.entities) byId.emplace(item.id, &item);
            if (const auto rect = GetGuiElementRect(*selected, byId, viewportWidth, viewportHeight)) {
                const ImVec2 min(viewportOrigin.x + rect->x, viewportOrigin.y + rect->y);
                const ImVec2 max(min.x + rect->width, min.y + rect->height);
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImU32 accent = ImGui::ColorConvertFloat4ToU32(ThemeColor(blubox::engine::ui::kDarkTheme.accent));
                draw->AddRect(min, max, accent, 0.0f, 0, 2.0f);
                for (const ImVec2& center : GuiResizeHandleCenters(*rect, viewportOrigin)) {
                    const ImVec2 handleMin(center.x - 4.0f, center.y - 4.0f);
                    const ImVec2 handleMax(center.x + 4.0f, center.y + 4.0f);
                    draw->AddRectFilled(handleMin, handleMax, IM_COL32(245, 248, 255, 255));
                    draw->AddRect(handleMin, handleMax, accent, 0.0f, 0, 1.5f);
                }
            }
        }
    }
    return handledClick;
}

void DrawViewport(StudioState& state) {
    if (!ImGui::Begin("Viewport", &state.showViewport)) { ImGui::End(); return; }
    const auto playState = state.playMode ? state.playMode->state()
                                          : blubox::engine::play::PlayModeState::Stopped;
    const bool simulationActive = playState == blubox::engine::play::PlayModeState::Running ||
                                  playState == blubox::engine::play::PlayModeState::Paused;
    const bool gameClientView = simulationActive &&
        state.playMode->mode() != blubox::engine::play::PlayModeType::Run;
    if (gameClientView)
        ImGui::TextDisabled("Play test · click to capture · Esc releases the pointer · Tab toggles capture");
    else if (simulationActive)
        ImGui::TextDisabled("Run mode · server simulation view · Stop to return to editing");
    else
        ImGui::TextDisabled("Click to focus · WASD move · right mouse look · Space / Ctrl up or down · W / E / R transform");
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x > 1.0f && available.y > 1.0f) {
        state.requestedWidth = static_cast<int>(available.x);
        state.requestedHeight = static_cast<int>(available.y);
        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        const ImVec2 imageSize(available.x, available.y);
        const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
        const ImTextureID viewportTexture =
            static_cast<ImTextureID>(static_cast<intptr_t>(state.viewport.texture.id));
        if (simulationActive) {
            // In Play/Run the client/server render target is a normal ImGui image.
            // The edit viewport keeps it as draw-list background so ImGuizmo can
            // still receive drags without an image item intercepting the hover.
            ImGui::Image(viewportTexture, imageSize, ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImGui::GetWindowDrawList()->AddImage(
                viewportTexture, imageMin, imageMax, ImVec2(0, 1), ImVec2(1, 0));
        }
        state.viewportHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(imageMin, imageMax, true);
        bool guiClickHandled = false;
        // ImGuizmo refuses to start a drag if ImGui reports any item as hovered.
        // The viewport image is deliberately drawn as background draw-list content,
        // but stale/overlapping ImGui hover IDs from the docked viewport can still
        // survive until this point and make the axes look clickable without moving.
        // The viewport has no interactive ImGui widgets over the scene, so give a
        // click inside its scene rectangle to the gizmo before calling Manipulate.
        if (state.viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImGuiContext* context = ImGui::GetCurrentContext();
            if (context && context->HoveredWindow == ImGui::GetCurrentWindow()) {
                context->HoveredId = 0;
                // No viewport scene controls are ImGui widgets: on a click in this
                // region, a stale active widget ID must not veto gizmo activation.
                ImGui::ClearActiveID();
            }
        }
        if (!simulationActive) guiClickHandled = HandleGuiViewportSelection(state, imageMin);
        if (!simulationActive && state.gizmoEnabled && state.selectedEntity >= 0 && state.selectedEntity < static_cast<int>(state.map.entities.size()) &&
            !IsScriptObject(state.map.entities[static_cast<std::size_t>(state.selectedEntity)]) &&
            state.map.entities[static_cast<std::size_t>(state.selectedEntity)].kind != "screengui" &&
            state.map.entities[static_cast<std::size_t>(state.selectedEntity)].kind != "frame" &&
            state.map.entities[static_cast<std::size_t>(state.selectedEntity)].kind != "textlabel" &&
            state.map.entities[static_cast<std::size_t>(state.selectedEntity)].kind != "textbutton") {
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetGizmoSizeClipSpace(0.15f);
            const Matrix viewMatrix = MatrixLookAt(state.camera.position, state.camera.target, state.camera.up);
            const Matrix projectionMatrix = MatrixPerspective(state.camera.fovy * DEG2RAD,
                imageSize.x / imageSize.y, 0.01, 1000.0);
            auto view = ToColumnMajor(viewMatrix);
            auto projection = ToColumnMajor(projectionMatrix);
            auto& entity = state.map.entities[static_cast<std::size_t>(state.selectedEntity)];
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                if (ImGui::IsKeyPressed(ImGuiKey_W, false)) state.gizmoOperation = ImGuizmo::TRANSLATE, state.gizmoEnabled = true;
                if (ImGui::IsKeyPressed(ImGuiKey_E, false)) state.gizmoOperation = ImGuizmo::ROTATE, state.gizmoEnabled = true;
                if (ImGui::IsKeyPressed(ImGuiKey_R, false)) state.gizmoOperation = ImGuizmo::SCALE, state.gizmoEnabled = true;
            }
            std::array<float, 3> translation = entity.transform.position;
            std::array<float, 3> rotation{};
            std::array<float, 3> scale = entity.transform.scale;
            for (std::size_t axis = 0; axis < rotation.size(); ++axis) {
                rotation[axis] = entity.transform.rotation[axis] * RAD2DEG;
            }
            std::array<float, 16> transform{};
            ImGuizmo::RecomposeMatrixFromComponents(translation.data(), rotation.data(), scale.data(), transform.data());
            ImGuizmo::PushID(state.selectedEntity + 1);
            const bool transformChanged = ImGuizmo::Manipulate(view.data(), projection.data(), state.gizmoOperation,
                                                               ImGuizmo::LOCAL, transform.data());
            ImGuizmo::PopID();
            if (transformChanged) {
                ImGuizmo::DecomposeMatrixToComponents(transform.data(), translation.data(), rotation.data(), scale.data());
                entity.transform.position = translation;
                entity.transform.scale = scale;
                state.dirty = true;
                for (std::size_t axis = 0; axis < rotation.size(); ++axis) {
                    entity.transform.rotation[axis] = rotation[axis] * DEG2RAD;
                }
            }
        }
        // Reserve edit-mode viewport layout after ImGuizmo has handled input.
        // ImGui::Image already reserves the game-view area during simulation.
        if (!simulationActive) ImGui::Dummy(imageSize);
        if (!simulationActive && state.viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !guiClickHandled &&
            !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            state.selectedEntity = PickEntity(state, mouse.x - imageMin.x, mouse.y - imageMin.y,
                                               imageSize.x, imageSize.y);
            state.viewportFocused = true;
        }
        if (state.viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) state.viewportFocused = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !state.viewportHovered &&
            !ImGuizmo::IsUsing()) state.viewportFocused = false;
    } else {
        state.viewportHovered = false;
    }
    ImGui::End();
}

void DrawCloudPanel(StudioState& state) {
    if (!ImGui::Begin("Blubox Cloud", &state.showCloudPanel)) { ImGui::End(); return; }
    ImGui::TextWrapped("Save your signed-in world to your Blubox account, or publish it for review. Use File > Save a local copy to keep a .blubox file on this PC.");
    ImGui::TextDisabled("Website API: %s", state.api.baseUrl().c_str());
    ImGui::Text("Account: %s", state.displayName.c_str());
    ImGui::InputText("World ID", &state.cloudWorldId);
    ImGui::TextDisabled("The website session is managed by Studio and never included in map data.");
    if (ImGui::Button("Save map to website", ImVec2(-1, 0))) SaveMap(state, false);
    if (ImGui::Button("Publish game...", ImVec2(-1, 0))) {
        state.publishTitle = state.map.name;
        state.publishDescription.clear();
        state.publishError.clear();
        state.showPublishDialog = true;
    }
    if (ImGui::Button("Load map from website", ImVec2(-1, 0))) {
        MapDocument downloaded;
        std::string error;
        if (state.api.DownloadMap(state.cloudWorldId, state.websiteToken, downloaded, error)) {
            state.map = std::move(downloaded);
            state.openScriptIds.clear();
            state.scriptEditors.clear();
            state.cloudWorldId = state.map.worldId;
            state.selectedEntity = -1;
            state.selectedService.clear();
            state.currentFilePath.clear();
            state.dirty = false;
            CreateScriptEngine(state);
            SetStatus(state, "World loaded from the Blubox website.");
        } else SetStatus(state, error, true);
    }
    ImGui::End();
}

void DrawWorldPanel(StudioState& state) {
    if (!ImGui::Begin("World", &state.showWorldPanel)) { ImGui::End(); return; }
    ImGui::TextDisabled("WORLD SETTINGS");
    ImGui::TextWrapped("Build locally, then use File > Save to store a .blubox file. Publish to Website sends the map to your account.");
    ImGui::InputText("New world", &state.newMapName);
    if (ImGui::Button("Create world", ImVec2(-1, 0))) {
        try {
            state.map = MapDocument::Create(state.newMapName);
            state.openScriptIds.clear();
            state.scriptEditors.clear();
            state.cloudWorldId.clear();
            state.selectedEntity = -1;
            state.selectedService.clear();
            state.currentFilePath.clear();
            state.dirty = true;
            CreateScriptEngine(state);
            SetStatus(state, "World created. Save a .blubox file or publish it when ready.");
        } catch (const std::exception& exception) { SetStatus(state, exception.what(), true); }
    }
    ImGui::End();
}

void DrawOutput(StudioState& state) {
    if (!ImGui::Begin("Output", &state.showOutput)) { ImGui::End(); return; }
    if (ImGui::SmallButton("Clear")) state.output.clear();
    ImGui::SameLine();
    ImGui::TextDisabled("Script errors and Studio messages");
    ImGui::Separator();
    ImGui::BeginChild("OutputLog", ImVec2(0.0f, 0.0f), true);
    const bool followTail = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
    for (const OutputEntry& entry : state.output) {
        const ImVec4 color = entry.level == "ERROR" ? ImVec4(0.96f, 0.38f, 0.35f, 1.0f)
            : entry.level == "WARNING" ? ImVec4(0.96f, 0.74f, 0.30f, 1.0f)
            : entry.level == "INFO" ? ImVec4(0.64f, 0.78f, 0.92f, 1.0f)
            : ImVec4(0.82f, 0.84f, 0.88f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(("[" + entry.level + "] " + entry.message).c_str());
        ImGui::PopStyleColor();
    }
    if (followTail) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

int RunStudio(std::string startupToken, std::string startupLinkError) {
    StudioState state;
    std::filesystem::path appDirectory(GetApplicationDirectory());
    std::filesystem::path catalogPath = appDirectory / "assets" / "editor" / "luau_api_catalog.json";
    for (int depth = 0; depth < 3 && !std::filesystem::exists(catalogPath); ++depth) {
        appDirectory = appDirectory.parent_path();
        if (appDirectory.empty()) break;
        catalogPath = appDirectory / "assets" / "editor" / "luau_api_catalog.json";
    }
    state.completionEngine.requestCatalogLoad(std::move(catalogPath));
    TraceLog(LOG_INFO, "Studio API endpoint: %s", state.api.baseUrl().c_str());
    AppendOutput(state, "INFO", "Blubox Studio is ready. Add objects from Explorer with +, then save with File > Save.");
    state.authError = std::move(startupLinkError);
    state.observedSessionFileTime = SessionStore::ModifiedTime();
    if (!startupToken.empty()) {
        blubox::studio::LogDiagnostic("INFO", "Primary Studio received an authentication URL; storing protected session.");
        std::string error;
        if (SessionStore::Save(startupToken, error)) {
            blubox::studio::LogDiagnostic("INFO", "Protected session stored; beginning validation.");
            state.observedSessionFileTime = SessionStore::ModifiedTime();
            BeginSessionValidation(state, std::move(startupToken));
        } else {
            blubox::studio::LogDiagnostic("ERROR", "Could not store protected sign-in session: " + error);
            state.authError = std::move(error);
        }
    } else {
        std::string error;
        auto savedToken = SessionStore::Load(error);
        if (savedToken) {
            blubox::studio::LogDiagnostic("INFO", "Found protected session at startup; beginning validation.");
            BeginSessionValidation(state, std::move(*savedToken));
        }
        else if (!error.empty()) {
            blubox::studio::LogDiagnostic("ERROR", "Could not load protected session: " + error);
            state.authError = std::move(error);
            std::string ignored;
            SessionStore::Clear(ignored);
        } else {
            blubox::studio::LogDiagnostic("INFO", "No saved session found at startup; login modal will be shown.");
        }
    }
    // Avoid forcing an MSAA pixel format. Some Windows WGL drivers reject that
    // format and fail to create the editor context even though OpenGL is available.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(kInitialWidth, kInitialHeight, "Blubox Studio");
    Image appIcon = LoadImageFromMemory(".png", blubox::kBrandIconData, blubox::kBrandIconDataSize);
    if (appIcon.data != nullptr) {
        ImageFormat(&appIcon, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        SetWindowIcon(appIcon);
        UnloadImage(appIcon);
    }
    if (!IsWindowReady()) throw std::runtime_error("Could not create the Blubox Studio window.");
    // Keep Escape available to dismiss UI and release the Play-mode pointer;
    // Raylib's default exit key would otherwise close the entire Studio process.
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    state.viewport = LoadRenderTexture(1280, 720);
    SetTextureFilter(state.viewport.texture, TEXTURE_FILTER_BILINEAR);
    state.textureCache = std::make_unique<blubox::engine::assets::TextureCache>();
    if (!CreateDecalQuad(state.decalQuad)) AppendOutput(state, "WARNING", "Decal preview mesh could not be uploaded.");
    rlImGuiSetup(true);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    ApplyStudioTheme();
    LoadStudioFonts();

    std::string lastWindowTitle;
    while (!WindowShouldClose() && !state.exitRequested) {
        if (state.textureCache) state.textureCache->pumpGpuUploads();
        PumpModelImport(state);
        RefreshRaylibMaterials(state);
        const std::string windowTitle = state.map.name + " - Blubox Studio";
        if (windowTitle != lastWindowTitle) {
            SetWindowTitle(windowTitle.c_str());
            lastWindowTitle = windowTitle;
        }
        PollSessionValidation(state);
        if (state.authenticated && state.projectOpen && state.scripts) {
            state.scripts->Tick(GetFrameTime());
            const std::string& scriptError = state.scripts->lastRuntimeError();
            if (!scriptError.empty() && scriptError != state.lastReportedScriptError) {
                AppendOutput(state, "ERROR", scriptError);
                state.lastReportedScriptError = scriptError;
            } else if (scriptError.empty()) state.lastReportedScriptError.clear();
        }
        if (!state.showViewport) {
            state.viewportHovered = false;
            state.viewportFocused = false;
            if (state.cameraLookCaptured) EnableCursor();
            state.cameraLookCaptured = false;
            SetGameCursorCaptured(state, false);
        }
        const bool runningPlayClient = state.playMode &&
            (state.playMode->state() == blubox::engine::play::PlayModeState::Running || state.playMode->state() == blubox::engine::play::PlayModeState::Paused) &&
            state.playMode->mode() != blubox::engine::play::PlayModeType::Run;
        if (!runningPlayClient) UpdateViewportCamera(state, GetFrameTime());
        if (runningPlayClient) {
            // Match the game's mouse capture controls: Tab releases/captures
            // the pointer, and clicking the viewport captures it again.
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (state.inputRouter.routeGameInput()) SetGameCursorCaptured(state, false);
            } else if (IsKeyPressed(KEY_TAB)) {
                SetGameCursorCaptured(state, !state.inputRouter.routeGameInput());
            } else if (!state.inputRouter.routeGameInput() && state.viewportHovered &&
                       IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                SetGameCursorCaptured(state, true);
            }
            if (state.playMode->state() == blubox::engine::play::PlayModeState::Running) {
                const MapDocument* clientWorld = state.playMode->clientWorld();
                if (clientWorld) {
                    const auto character = std::find_if(clientWorld->entities.begin(), clientWorld->entities.end(),
                        [](const Entity& item) { return item.name == "LocalPlayerCharacter"; });
                    if (character != clientWorld->entities.end()) {
                    using namespace blubox::engine::input;
                    PhysicalInput physical;
                    const bool gameInputEnabled = state.inputRouter.routeGameInput();
                    physical.forward = gameInputEnabled && IsKeyDown(KEY_W);
                    physical.backward = gameInputEnabled && IsKeyDown(KEY_S);
                    physical.left = gameInputEnabled && IsKeyDown(KEY_A);
                    physical.right = gameInputEnabled && IsKeyDown(KEY_D);
                    physical.jump = gameInputEnabled && IsKeyDown(KEY_SPACE);
                    physical.sprint = gameInputEnabled && IsKeyDown(KEY_LEFT_SHIFT);
                    if (gameInputEnabled) {
                        const Vector2 mouse = GetMouseDelta();
                        physical.mouseDeltaX = mouse.x;
                        physical.mouseDeltaY = mouse.y;
                        physical.mouseWheel = GetMouseWheelMove();
                    }
                    auto command = state.inputSystem.createCommand(physical, state.gameCamera.yaw, state.gameCamera.pitch);
                    const auto& position = character->transform.position;
                    const float dt = std::clamp(GetFrameTime(), 0.0f, 0.1f);
                    blubox::engine::camera::GameCameraSystem::update(state.gameCamera,
                        {position[0],position[1],position[2]}, {}, command, dt);
                    command.yaw = state.gameCamera.yaw;
                    command.pitch = state.gameCamera.pitch;
                    state.playMode->sendInput(command);
                    state.camera.position = {state.gameCamera.position.x,state.gameCamera.position.y,state.gameCamera.position.z};
                    state.camera.target = {state.gameCamera.position.x + state.gameCamera.forward.x,
                                            state.gameCamera.position.y + state.gameCamera.forward.y,
                                            state.gameCamera.position.z + state.gameCamera.forward.z};
                    state.camera.fovy = state.gameCamera.fieldOfView;
                    }
                }
            }
        }
        if (state.authenticated && state.projectOpen && state.scripts) {
            state.scripts->RenderTick(GetFrameTime());
        }
        if (state.playMode) {
            state.playMode->tick(GetFrameTime());
            if (state.playMode->state() == blubox::engine::play::PlayModeState::Failed) {
                SetGameCursorCaptured(state, false);
                if (state.editorCameraSaved) { state.camera = state.editorCamera; state.editorCameraSaved = false; }
                const std::string playError = state.playMode->lastError();
                if (!playError.empty() && state.lastReportedScriptError != playError) {
                    state.lastReportedScriptError = playError;
                    AppendOutput(state, "ERROR", "Local Play: " + playError);
                }
            }
        }
        if (state.selectedEntity >= static_cast<int>(state.map.entities.size())) state.selectedEntity = -1;
        BeginDrawing();
        ClearBackground(Color{19, 19, 26, 255});
        if (state.authenticated && state.projectOpen) {
            const MapDocument* renderWorld = &state.map;
            if (state.playMode) {
                const auto playState = state.playMode->state();
                if (playState == blubox::engine::play::PlayModeState::Running ||
                    playState == blubox::engine::play::PlayModeState::Paused) {
                    if (state.playMode->mode() == blubox::engine::play::PlayModeType::Run) {
                        if (std::chrono::steady_clock::now() >= state.nextServerViewRefresh || !state.runtimeServerWorldCache) {
                            state.runtimeServerWorldCache = state.playMode->serverWorldSnapshot();
                            state.nextServerViewRefresh = std::chrono::steady_clock::now() + std::chrono::milliseconds(33);
                        }
                        if (state.runtimeServerWorldCache) renderWorld = &*state.runtimeServerWorldCache;
                    } else if (const MapDocument* localClientWorld = state.playMode->clientWorld()) renderWorld = localClientWorld;
                }
            }
            const bool simulationView = state.playMode &&
                (state.playMode->state() == blubox::engine::play::PlayModeState::Running ||
                 state.playMode->state() == blubox::engine::play::PlayModeState::Paused);
            const bool showEditorGrid = !simulationView ||
                state.playMode->mode() == blubox::engine::play::PlayModeType::Run;
            DrawWorld(state, *renderWorld, simulationView, showEditorGrid);
        }
        rlImGuiBegin();
        // Required once per ImGui frame: refresh ImGuizmo's hover/drag state before
        // any viewport calls to Manipulate().
        ImGuizmo::BeginFrame();
        if (!state.authenticated) {
            DrawLoginModal(state);
        } else if (state.projectOpen) {
            if (state.inputRouter.routeEditorInput()) HandleShortcuts(state);
            DrawTopBar(state);
            BeginStudioWorkspace(state);
            DrawNewMapConfirmation(state);
            DrawOpenMapConfirmation(state);
            DrawPublishDialog(state);
            if (state.authenticated && state.projectOpen) {
                if (state.showExplorer) DrawHierarchy(state);
                if (state.showProperties) DrawInspector(state);
                if (state.showViewport) DrawViewport(state);
                DrawScriptPanel(state);
                if (state.showOutput) DrawOutput(state);
                if (state.showWorldPanel) DrawWorldPanel(state);
                if (state.showCloudPanel) DrawCloudPanel(state);
            } else DrawLoginModal(state);
            ImGui::End(); // workspace host
        }
        rlImGuiEnd();
        EndDrawing();
        EnsureViewport(state);
    }

    rlImGuiShutdown();
    if (state.viewport.texture.id != 0) UnloadRenderTexture(state.viewport);
    std::fill(state.websiteToken.begin(), state.websiteToken.end(), '\0');
    std::fill(state.pendingHandoffToken.begin(), state.pendingHandoffToken.end(), '\0');
    if (state.playMode) state.playMode->stop();
    SetGameCursorCaptured(state, false);
    if (state.modelImportTask.valid()) state.modelImportTask.wait();
    ReleaseProjectAssets(state);
    state.textureCache.reset();
    if (state.decalQuad.vaoId != 0 || state.decalQuad.vboId != nullptr) UnloadMesh(state.decalQuad);
    CloseWindow();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (TryRunStudioUpdate(argc, argv)) return 0;

    std::string startupToken;
    std::string startupLinkError;
    bool debugConsole = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--debug-console") {
            debugConsole = true;
            continue;
        }
        std::string token;
        std::string error;
        if (blubox::studio::ParseStudioAuthUrl(argv[i], token, error)) {
            startupToken = std::move(token);
        } else if (!error.empty()) startupLinkError = std::move(error);
    }

    const bool debugConsoleRequested = debugConsole || blubox::studio::DebugConsoleRequested();
    blubox::studio::InitializeDiagnostics(debugConsoleRequested);
    if (debugConsoleRequested) {
        TraceLog(LOG_INFO, "Studio file diagnostics enabled; authentication tokens are redacted.");
    }
    if (!startupLinkError.empty()) {
        blubox::studio::LogDiagnostic("ERROR", startupLinkError);
        TraceLog(LOG_ERROR, "Studio authentication link was rejected: %s", startupLinkError.c_str());
    } else if (!startupToken.empty()) {
        blubox::studio::LogDiagnostic("INFO", "Authentication URL received; token redacted.");
        TraceLog(LOG_INFO, "Studio authentication link received (token redacted).");
    }

    try {
        blubox::studio::StudioInstanceLock instanceLock;
        if (!instanceLock.IsPrimary()) {
            blubox::studio::LogDiagnostic("INFO", "Protocol handler started a secondary process; forwarding URL to primary Studio.");
            if (!startupToken.empty()) {
                std::string error;
                if (!SessionStore::Save(startupToken, error)) {
                    blubox::studio::LogDiagnostic("ERROR", "Could not save protected sign-in handoff: " + error);
                    TraceLog(LOG_ERROR, "Could not forward Studio sign-in to the open window: %s", error.c_str());
                    return 1;
                }
                blubox::studio::LogDiagnostic("INFO", "Protected sign-in handoff saved and primary process signaled.");
            } else if (!startupLinkError.empty()) {
                blubox::studio::LogDiagnostic("ERROR", "Secondary process received a malformed authentication URL: " + startupLinkError);
            }
            return 0;
        }
        blubox::studio::LogDiagnostic("INFO", "This process owns the primary Studio window.");
        return RunStudio(std::move(startupToken), std::move(startupLinkError));
    } catch (const std::exception& exception) {
        blubox::studio::LogDiagnostic("FATAL", exception.what());
        TraceLog(LOG_ERROR, "Blubox Studio: %s", exception.what());
#ifdef _WIN32
        MessageBoxA(nullptr, exception.what(), "Blubox Studio", MB_OK | MB_ICONERROR);
#endif
        return 1;
    } catch (...) {
        blubox::studio::LogDiagnostic("FATAL", "Unknown startup exception.");
        TraceLog(LOG_ERROR, "Blubox Studio: unknown startup exception.");
#ifdef _WIN32
        MessageBoxA(nullptr, "An unknown startup error occurred.", "Blubox Studio", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
