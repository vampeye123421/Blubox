#include "MapDocument.hpp"
#include "AuthSession.hpp"
#include "Diagnostics.hpp"
#include "ScriptEngine.hpp"
#include "WebsiteApi.hpp"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <rlImGui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <chrono>
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
#undef ShowCursor
#undef CloseWindow
#undef Rectangle
#endif

namespace {

using blubox::studio::Entity;
using blubox::studio::MapDocument;
using blubox::studio::SessionStore;
using blubox::studio::WebsiteApi;

constexpr int kInitialWidth = 1440;
constexpr int kInitialHeight = 900;

ImFont* g_codeFont = nullptr;

ImVec4 Rgb(unsigned int color, float alpha = 1.0f) {
    return ImVec4(
        static_cast<float>((color >> 16) & 0xff) / 255.0f,
        static_cast<float>((color >> 8) & 0xff) / 255.0f,
        static_cast<float>(color & 0xff) / 255.0f,
        alpha);
}

void ApplyStudioTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Blubox dark palette from the website's Studio sign-in and dark theme.
    const ImVec4 background = Rgb(0x13131a);
    const ImVec4 surface = Rgb(0x1e1e28);
    const ImVec4 inset = Rgb(0x0c0c10);
    const ImVec4 border = Rgb(0x2e2e3e);
    const ImVec4 text = Rgb(0xe2e8f0);
    const ImVec4 muted = Rgb(0x94a3b8);
    const ImVec4 accent = Rgb(0x00d2ff);
    const ImVec4 accentHover = Rgb(0x38e0ff);

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = muted;
    colors[ImGuiCol_WindowBg] = background;
    colors[ImGuiCol_ChildBg] = surface;
    colors[ImGuiCol_PopupBg] = surface;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = Rgb(0x000000, 0.0f);
    colors[ImGuiCol_FrameBg] = inset;
    colors[ImGuiCol_FrameBgHovered] = Rgb(0x263944);
    colors[ImGuiCol_FrameBgActive] = Rgb(0x304b58);
    colors[ImGuiCol_TitleBg] = surface;
    colors[ImGuiCol_TitleBgActive] = surface;
    colors[ImGuiCol_TitleBgCollapsed] = background;
    colors[ImGuiCol_MenuBarBg] = surface;
    colors[ImGuiCol_ScrollbarBg] = background;
    colors[ImGuiCol_ScrollbarGrab] = border;
    colors[ImGuiCol_ScrollbarGrabHovered] = muted;
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentHover;
    colors[ImGuiCol_Button] = Rgb(0x183844);
    colors[ImGuiCol_ButtonHovered] = Rgb(0x20566a);
    colors[ImGuiCol_ButtonActive] = Rgb(0x26718a);
    colors[ImGuiCol_Header] = Rgb(0x183844);
    colors[ImGuiCol_HeaderHovered] = Rgb(0x20566a);
    colors[ImGuiCol_HeaderActive] = Rgb(0x26718a);
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accentHover;
    colors[ImGuiCol_ResizeGrip] = Rgb(0x00d2ff, 0.22f);
    colors[ImGuiCol_ResizeGripHovered] = Rgb(0x00d2ff, 0.68f);
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_Tab] = background;
    colors[ImGuiCol_TabHovered] = Rgb(0x20566a);
    colors[ImGuiCol_TabActive] = surface;
    colors[ImGuiCol_TabUnfocused] = background;
    colors[ImGuiCol_TabUnfocusedActive] = surface;
    colors[ImGuiCol_DockingPreview] = Rgb(0x00d2ff, 0.35f);
    colors[ImGuiCol_DockingEmptyBg] = background;
    colors[ImGuiCol_TableHeaderBg] = surface;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = Rgb(0x2e2e3e, 0.65f);
    colors[ImGuiCol_TableRowBg] = Rgb(0x1e1e28, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = Rgb(0x94a3b8, 0.045f);
    colors[ImGuiCol_TextSelectedBg] = Rgb(0x00d2ff, 0.25f);
    colors[ImGuiCol_DragDropTarget] = accentHover;
    colors[ImGuiCol_NavHighlight] = accent;
    colors[ImGuiCol_NavWindowingHighlight] = Rgb(0xe2e8f0, 0.7f);
    colors[ImGuiCol_NavWindowingDimBg] = Rgb(0x0c0c10, 0.3f);
    colors[ImGuiCol_ModalWindowDimBg] = Rgb(0x0c0c10, 0.62f);

    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 19.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 12.0f;
    style.WindowRounding = 9.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 9.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 7.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
}

void LoadStudioFonts() {
    const std::filesystem::path fontDirectory =
        std::filesystem::path(GetApplicationDirectory()) / "assets" / "editor" / "fonts";
    const std::filesystem::path interfaceFont = fontDirectory / "InterVariable.ttf";
    const std::filesystem::path codeFont = fontDirectory / "JetBrainsMono-Regular.ttf";

    ImGuiIO& io = ImGui::GetIO();
    if (ImFont* font = io.Fonts->AddFontFromFileTTF(interfaceFont.string().c_str(), 16.0f)) {
        io.FontDefault = font;
    } else {
        TraceLog(LOG_WARNING, "Blubox Studio: Inter font not found at %s; using ImGui default.",
                 interfaceFont.string().c_str());
    }
    g_codeFont = io.Fonts->AddFontFromFileTTF(codeFont.string().c_str(), 15.0f);
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

struct StudioState {
    WebsiteApi api;
    MapDocument map = MapDocument::Create("My First World");
    std::unique_ptr<blubox::studio::ScriptEngine> scripts;
    RenderTexture2D viewport{};
    Camera3D camera{{15.0f, 12.0f, 15.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 55.0f, CAMERA_PERSPECTIVE};
    int selectedEntity = -1;
    std::string selectedService;
    int requestedWidth = 0;
    int requestedHeight = 0;
    ImGuizmo::OPERATION gizmoOperation = ImGuizmo::TRANSLATE;
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
    std::filesystem::path currentFilePath;
    bool statusIsError = false;
    bool dirty = false;
    bool showExplorer = true;
    bool showProperties = true;
    bool showScriptEditor = true;
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
    bool cameraLookCaptured = false;
    bool cameraAnglesInitialized = false;
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;
    bool authenticated = false;
    bool validatingSession = false;
    bool loginBrowserOpened = false;
    bool projectOpen = false;
    std::future<SessionValidation> validationTask;
    std::future<StudioHandoffPoll> handoffPollTask;
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
        } else if (kind == "script" || kind == "localscript") {
            object.luauScript = "-- Runs in Blubox Studio's local Luau preview.\n"
                                "-- Example: body.onTick(function(deltaTime)\n"
                                "--     -- Update this object here.\n"
                                "-- end)\n";
        }
        state.selectedEntity = static_cast<int>(state.map.entities.size() - 1);
        state.selectedService.clear();
        state.showScriptEditor = true;
        state.showExplorer = true;
        state.dirty = true;
        AppendOutput(state, "INFO", "Created " + object.name + " in " + parent + ".");
        SetStatus(state, "Created " + object.name + ".");
    } catch (const std::exception& exception) {
        SetStatus(state, exception.what(), true);
    }
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

bool SaveMap(StudioState& state, bool choosePath) {
    std::filesystem::path path = state.currentFilePath;
    if (choosePath || path.empty()) {
        auto selected = ChooseMapPath(true);
        if (!selected) return false;
        path = std::move(*selected);
    }
    try {
        const auto json = state.map.ToJson();
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Could not open the selected file for writing.");
        output << json.dump(2) << '\n';
        if (!output) throw std::runtime_error("The map file could not be written completely.");
        state.currentFilePath = path;
        state.dirty = false;
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
        state.map = std::move(opened);
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
        state.map = MapDocument::Create("Untitled World");
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
            if (!state.validatingSession && result.token != state.websiteToken) {
                blubox::studio::LogDiagnostic("INFO", "Website delivered the browser session through the one-time handoff; token redacted.");
                state.pendingHandoffId.clear();
                state.handoffDeadline = {};
                std::string saveError;
                if (SessionStore::Save(result.token, saveError)) {
                    state.observedSessionFileTime = SessionStore::ModifiedTime();
                    BeginSessionValidation(state, std::move(result.token));
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
        if (result.valid) {
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
    if (!state.pendingHandoffId.empty()) return;
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
        state.authError.clear();
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
    state.selectedEntity = -1;
    state.selectedService.clear();
    state.cloudWorldId.clear();
    state.projectOpen = false;
    state.authenticated = false;
    state.validatingSession = false;
    state.loginBrowserOpened = false;
    state.pendingHandoffId.clear();
    state.handoffDeadline = {};
    state.authError.clear();
    state.displayName.clear();
    std::fill(state.websiteToken.begin(), state.websiteToken.end(), '\0');
    state.websiteToken.clear();
    state.observedSessionFileTime = SessionStore::ModifiedTime();
    SetStatus(state, "Signed out. Sign in to open a project.");
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

void DrawWorld(const StudioState& state) {
    BeginTextureMode(state.viewport);
    ClearBackground(Color{123, 177, 207, 255});
    BeginMode3D(state.camera);
    DrawGrid(48, 1.0f);
    for (std::size_t i = 0; i < state.map.entities.size(); ++i) {
        const Entity& entity = state.map.entities[i];
        if (!entity.visible || (entity.kind != "block" && entity.kind != "sphere") || entity.parent != "Workspace") continue;
        const auto& t = entity.transform;
        const Color color = state.selectedEntity == static_cast<int>(i)
            ? Color{255, 207, 92, 255}
            : Color{entity.color[0], entity.color[1], entity.color[2], entity.color[3]};
        rlPushMatrix();
        rlTranslatef(t.position[0], t.position[1], t.position[2]);
        rlRotatef(t.rotation[0] * RAD2DEG, 1.0f, 0.0f, 0.0f);
        rlRotatef(t.rotation[1] * RAD2DEG, 0.0f, 1.0f, 0.0f);
        rlRotatef(t.rotation[2] * RAD2DEG, 0.0f, 0.0f, 1.0f);
        const Vector3 dimensions{t.scale[0], t.scale[1], t.scale[2]};
        if (entity.kind == "sphere") DrawSphere(Vector3{0.0f, 0.0f, 0.0f}, t.scale[0] * 0.5f, color);
        else DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, dimensions, color);
        rlPopMatrix();
    }
    EndMode3D();
    std::function<void(const std::string&, int, int)> drawGuiChildren;
    drawGuiChildren = [&](const std::string& parent, int originX, int originY) {
        for (const Entity& entity : state.map.entities) {
            if (entity.parent != parent || entity.kind == "screengui" ||
                entity.kind == "script" || entity.kind == "localscript") continue;
            if (entity.kind != "frame" && entity.kind != "textlabel" && entity.kind != "textbutton") continue;
            if (!entity.properties.value("visible", true)) continue;
            const auto position = entity.properties.value("position", nlohmann::json::array({40, 40}));
            const auto size = entity.properties.value("size", nlohmann::json::array({220, 64}));
            if (!position.is_array() || position.size() != 2 || !size.is_array() || size.size() != 2) continue;
            const int x = originX + position[0].get<int>();
            const int y = originY + position[1].get<int>();
            const int width = std::max(1, size[0].get<int>());
            const int height = std::max(1, size[1].get<int>());
            const auto readColor = [](const nlohmann::json& values, Color fallback) {
                if (!values.is_array() || values.size() != 4) return fallback;
                return Color{static_cast<unsigned char>(std::clamp(values[0].get<int>(), 0, 255)),
                             static_cast<unsigned char>(std::clamp(values[1].get<int>(), 0, 255)),
                             static_cast<unsigned char>(std::clamp(values[2].get<int>(), 0, 255)),
                             static_cast<unsigned char>(std::clamp(values[3].get<int>(), 0, 255))};
            };
            DrawRectangle(x, y, width, height,
                readColor(entity.properties.value("backgroundColor", nlohmann::json::array()), Color{34, 44, 58, 230}));
            if (entity.kind == "textlabel" || entity.kind == "textbutton") {
                const std::string text = entity.properties.value("text", std::string{});
                const Color textColor = readColor(entity.properties.value("textColor", nlohmann::json::array()), WHITE);
                DrawText(text.c_str(), x + 10, y + std::max(4, (height - 20) / 2), 20, textColor);
            }
            if (entity.kind == "frame") drawGuiChildren(entity.id, x, y);
        }
    };
    for (const Entity& screenGui : state.map.entities) {
        if (screenGui.kind == "screengui" && screenGui.parent == "StarterGui" &&
            screenGui.properties.value("enabled", true)) {
            drawGuiChildren(screenGui.id, 0, 0);
        }
    }
    EndTextureMode();
}

void DrawTopBar(StudioState& state) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New world", "Ctrl+N")) state.pendingNewMap = true;
        if (ImGui::MenuItem("Open...", "Ctrl+O")) state.pendingOpenMap = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) SaveMap(state, false);
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) SaveMap(state, true);
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
        ImGui::MenuItem("Luau Script", nullptr, &state.showScriptEditor);
        ImGui::MenuItem("Output", nullptr, &state.showOutput);
        ImGui::Separator();
        ImGui::MenuItem("World settings", nullptr, &state.showWorldPanel);
        ImGui::MenuItem("Cloud publishing", nullptr, &state.showCloudPanel);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Insert")) {
        if (ImGui::MenuItem("Block")) AddEditorObject(state, "block", "Workspace");
        if (ImGui::MenuItem("Sphere")) AddEditorObject(state, "sphere", "Workspace");
        ImGui::EndMenu();
    }
    if (ImGui::Button("Save")) SaveMap(state, false);
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

void SetupDefaultDockLayout() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspaceId = ImGui::GetID("BluboxStudioDockspace");
    if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr) return;
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);
    ImGuiID center = dockspaceId;
    ImGuiID left = 0;
    ImGuiID right = 0;
    ImGuiID lower = 0;
    ImGuiID scriptPane = 0;
    ImGuiID outputPane = 0;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.27f, &lower, &center);
    ImGui::DockBuilderSplitNode(lower, ImGuiDir_Right, 0.42f, &outputPane, &scriptPane);
    ImGui::DockBuilderDockWindow("Explorer", left);
    ImGui::DockBuilderDockWindow("Properties", right);
    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderDockWindow("Luau Script", scriptPane);
    ImGui::DockBuilderDockWindow("Output", outputPane);
    ImGui::DockBuilderFinish(dockspaceId);
}

void DrawLoginModal(StudioState& state) {
    ImGui::OpenPopup("Accedi a Blubox Studio");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Accedi a Blubox Studio", nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Accedi con il tuo account Blubox nel browser per aprire Studio.");
        ImGui::Spacing();
        if (state.validatingSession) {
            ImGui::TextDisabled("Verifica della sessione in corso...");
        } else if (!state.pendingHandoffId.empty()) {
            ImGui::TextDisabled("In attesa del completamento dell'accesso nel browser...");
        } else if (ImGui::Button("Accedi con il Web", ImVec2(-1.0f, 42.0f))) {
            StartBrowserLogin(state);
        }
        if (state.loginBrowserOpened && !state.validatingSession) {
            ImGui::TextWrapped("Completa l'accesso nel browser. Studio completerà l'accesso automaticamente.");
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

void DrawAddMenu(StudioState& state, const std::string& parent, bool allowGeometry,
                 bool allowScript, bool allowLocalScript, bool allowGui = false) {
    ImGui::PushID(parent.c_str());
    if (ImGui::SmallButton("+")) ImGui::OpenPopup("Add object");
    if (ImGui::BeginPopup("Add object")) {
        if (allowGeometry && ImGui::MenuItem("Block")) AddEditorObject(state, "block", parent);
        if (allowGeometry && ImGui::MenuItem("Sphere")) AddEditorObject(state, "sphere", parent);
        if (allowScript && ImGui::MenuItem("Script")) AddEditorObject(state, "script", parent);
        if (allowLocalScript && ImGui::MenuItem("LocalScript")) AddEditorObject(state, "localscript", parent);
        if (allowGui && parent == "StarterGui" && ImGui::MenuItem("ScreenGui")) AddEditorObject(state, "screengui", parent);
        if (allowGui && parent != "StarterGui" && ImGui::MenuItem("Frame")) AddEditorObject(state, "frame", parent);
        if (allowGui && parent != "StarterGui" && ImGui::MenuItem("TextLabel")) AddEditorObject(state, "textlabel", parent);
        if (allowGui && parent != "StarterGui" && ImGui::MenuItem("TextButton")) AddEditorObject(state, "textbutton", parent);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void SelectHierarchyItem(StudioState& state, std::size_t index) {
    state.selectedEntity = static_cast<int>(index);
    state.selectedService.clear();
    state.showProperties = true;
    if (IsScriptObject(state.map.entities[index]) || !state.map.entities[index].luauScript.empty()) {
        state.showScriptEditor = true;
    }
}

void DrawEntityTree(StudioState& state, const std::string& parent) {
    for (std::size_t i = 0; i < state.map.entities.size(); ++i) {
        Entity& entity = state.map.entities[i];
        if (entity.parent != parent) continue;
        const std::string id = entity.id;
        const std::string name = entity.name;
        const std::string kind = entity.kind;
        const bool scriptObject = IsScriptObject(entity);
        ImGui::PushID(id.c_str());
        const bool hasChildren = std::any_of(state.map.entities.begin(), state.map.entities.end(),
            [&](const Entity& child) { return child.parent == id; });
        bool open = false;
        if (hasChildren) {
            const std::string label = scriptObject
                ? name + "  [" + (kind == "localscript" ? "LocalScript" : "Script") + "]"
                : name + "  [" + kind + "]";
            open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_OpenOnArrow |
                (state.selectedEntity == static_cast<int>(i) ? ImGuiTreeNodeFlags_Selected : 0));
            if (ImGui::IsItemClicked()) SelectHierarchyItem(state, i);
        } else {
            const std::string label = scriptObject
                ? name + "  [" + (kind == "localscript" ? "LocalScript" : "Script") + "]"
                : name + "  [" + kind + "]";
            if (ImGui::Selectable(label.c_str(), state.selectedEntity == static_cast<int>(i))) {
                SelectHierarchyItem(state, i);
            }
        }
        if (!scriptObject) {
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 38.0f));
            DrawAddMenu(state, id, false, true, false, kind == "screengui" || kind == "frame");
        }
        if (open) {
            DrawEntityTree(state, id);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void DrawHierarchy(StudioState& state) {
    if (!ImGui::Begin("Explorer", &state.showExplorer)) { ImGui::End(); return; }
    ImGui::TextDisabled("GAME OBJECTS");
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 38.0f));
    DrawAddMenu(state, "Workspace", true, true, false);
    ImGui::Separator();

    struct Service { const char* name; bool addGeometry; bool addScript; bool addLocalScript; };
    constexpr Service services[] = {
        {"Workspace", true, true, false},
        {"ReplicatedStorage", false, true, false},
        {"ServerScriptService", false, true, false},
        {"ServerStorage", false, true, false},
        {"StarterPlayer", false, true, true},
        {"StarterGui", false, true, true},
    };

    for (std::size_t serviceIndex = 0; serviceIndex < std::size(services); ++serviceIndex) {
        const Service& service = services[serviceIndex];
        ImGui::PushID(static_cast<int>(serviceIndex));
        ImGui::SetNextItemOpen(serviceIndex == 0, ImGuiCond_Once);
        const bool serviceOpen = ImGui::TreeNodeEx(service.name, ImGuiTreeNodeFlags_OpenOnArrow);
        if (ImGui::IsItemClicked()) {
            state.selectedEntity = -1;
            state.selectedService = service.name;
            state.showProperties = true;
        }
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 38.0f));
        DrawAddMenu(state, service.name, service.addGeometry, service.addScript, service.addLocalScript,
                    std::string(service.name) == "StarterGui");
        if (serviceOpen) {
            DrawEntityTree(state, service.name);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void DrawInspector(StudioState& state) {
    if (!ImGui::Begin("Properties", &state.showProperties)) { ImGui::End(); return; }
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
        ImGui::End();
        return;
    }
    if (state.selectedEntity < 0 || state.selectedEntity >= static_cast<int>(state.map.entities.size())) {
        ImGui::TextDisabled("Select an object in Explorer to edit it.");
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
    if (IsScriptObject(entity)) {
        ImGui::Text("Parent  %s", entity.parent.c_str());
        ImGui::TextWrapped("Scripts are saved with this .blubox map. Use the Luau Script panel to edit and preview the source.");
    } else if (entity.kind == "screengui") {
        bool enabled = entity.properties.value("enabled", true);
        bool ignoreInset = entity.properties.value("ignoreGuiInset", false);
        bool resetOnSpawn = entity.properties.value("resetOnSpawn", true);
        if (ImGui::Checkbox("Enabled", &enabled)) entity.properties["enabled"] = enabled, state.dirty = true;
        if (ImGui::Checkbox("Ignore GUI inset", &ignoreInset)) entity.properties["ignoreGuiInset"] = ignoreInset, state.dirty = true;
        if (ImGui::Checkbox("Reset on spawn", &resetOnSpawn)) entity.properties["resetOnSpawn"] = resetOnSpawn, state.dirty = true;
        ImGui::TextWrapped("Add Frames, TextLabels and TextButtons as children. They render as a HUD preview.");
    } else if (entity.kind == "frame" || entity.kind == "textlabel" || entity.kind == "textbutton") {
        int position[2] = {40, 40}, size[2] = {220, 64};
        const auto readPair = [](const nlohmann::json& value, int out[2]) {
            if (value.is_array() && value.size() == 2) {
                out[0] = value[0].get<int>(); out[1] = value[1].get<int>();
            }
        };
        readPair(entity.properties.value("position", nlohmann::json::array()), position);
        readPair(entity.properties.value("size", nlohmann::json::array()), size);
        if (ImGui::DragInt2("Position", position, 1.0f, -10000, 10000)) entity.properties["position"] = {position[0], position[1]}, state.dirty = true;
        if (ImGui::DragInt2("Size", size, 1.0f, 1, 10000)) entity.properties["size"] = {size[0], size[1]}, state.dirty = true;
        bool visible = entity.properties.value("visible", true);
        if (ImGui::Checkbox("Visible", &visible)) entity.properties["visible"] = visible, state.dirty = true;
        if (entity.kind != "frame") {
            std::string text = entity.properties.value("text", std::string{});
            if (ImGui::InputText("Text", &text)) entity.properties["text"] = text, state.dirty = true;
        }
        for (const auto& [label, key] : {std::pair{"Background color", "backgroundColor"}, std::pair{"Text color", "textColor"}}) {
            int channels[4] = {34, 44, 58, 230};
            const auto colors = entity.properties.value(key, nlohmann::json::array());
            if (colors.is_array() && colors.size() == 4) for (int i = 0; i < 4; ++i) channels[i] = colors[i].get<int>();
            float color[4]; for (int i = 0; i < 4; ++i) color[i] = std::clamp(channels[i] / 255.0f, 0.0f, 1.0f);
            if (ImGui::ColorEdit4(label, color)) {
                for (int i = 0; i < 4; ++i) channels[i] = static_cast<int>(color[i] * 255.0f + 0.5f);
                entity.properties[key] = {channels[0], channels[1], channels[2], channels[3]};
                state.dirty = true;
            }
        }
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("TRANSFORM");
        if (ImGui::DragFloat3("Position", entity.transform.position.data(), 0.1f)) state.dirty = true;
        if (ImGui::DragFloat3("Rotation", entity.transform.rotation.data(), 0.01f, -6.283f, 6.283f, "%.3f rad")) state.dirty = true;
        if (ImGui::DragFloat3("Scale", entity.transform.scale.data(), 0.05f, 0.001f, 100000.0f)) state.dirty = true;
    }
    ImGui::Spacing();
    if (ImGui::Button(IsScriptObject(entity) ? "Delete script" : "Delete object", ImVec2(-1, 0))) {
        const std::string deletedId = entity.id;
        const std::string deletedName = entity.name;
        std::erase_if(state.map.entities, [&](const Entity& candidate) {
            return candidate.id == deletedId || candidate.parent == deletedId;
        });
        state.selectedEntity = -1;
        state.selectedService.clear();
        state.dirty = true;
        AppendOutput(state, "INFO", "Deleted " + deletedName + ".");
    }
    ImGui::End();
}

void DrawScriptPanel(StudioState& state) {
    if (!ImGui::Begin("Luau Script", &state.showScriptEditor)) { ImGui::End(); return; }
    if (state.selectedEntity < 0 || state.selectedEntity >= static_cast<int>(state.map.entities.size())) {
        ImGui::TextDisabled("Select a Script or LocalScript in Explorer, or add one with +.");
        ImGui::End();
        return;
    }
    Entity& entity = state.map.entities[static_cast<std::size_t>(state.selectedEntity)];
    if (!IsScriptObject(entity) && entity.luauScript.empty()) {
        ImGui::TextDisabled("This part has no script yet. Use its + menu in Explorer to add a Script.");
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("SCRIPT  /  %s", entity.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%zu / 262144 bytes", entity.luauScript.size());
    if (entity.kind == "localscript") {
        ImGui::TextWrapped("LocalScript source is saved here and can be previewed in the editor sandbox. A separate client runtime and player replication are not implemented yet.");
    } else {
        ImGui::TextWrapped("Runs in the local Luau preview sandbox. Source is stored in the .blubox map.");
    }
    ImGui::PushItemWidth(-1.0f);
    const float editorHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y - 72.0f);
    if (g_codeFont) ImGui::PushFont(g_codeFont);
    const bool changed = ImGui::InputTextMultiline("##LuauSource", &entity.luauScript,
        ImVec2(-1.0f, editorHeight), ImGuiInputTextFlags_AllowTabInput);
    if (g_codeFont) ImGui::PopFont();
    ImGui::PopItemWidth();
    if (changed) state.dirty = true;
    if (changed && state.scripts) state.scripts->StopEntity(entity.id);
    if (ImGui::Button("Run / Apply", ImVec2(120, 0))) {
        std::string error;
        if (state.scripts->RunEntity(entity.id, error)) {
            const std::string message = entity.kind == "localscript"
                ? "LocalScript running in the editor preview sandbox."
                : "Script running in the editor preview sandbox.";
            AppendOutput(state, "INFO", message);
            SetStatus(state, message);
        }
        else SetStatus(state, error, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(80, 0))) {
        state.scripts->StopEntity(entity.id);
        SetStatus(state, "Luau script stopped.");
    }
    if (!state.scripts->lastRuntimeError().empty()) {
        ImGui::TextColored(StatusColor(true), "%s", state.scripts->lastRuntimeError().c_str());
    }
    ImGui::End();
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
        if (!entity.visible || (entity.kind != "block" && entity.kind != "sphere") || entity.parent != "Workspace") continue;
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
        if (IsKeyPressed(KEY_W)) state.gizmoOperation = ImGuizmo::TRANSLATE;
        if (IsKeyPressed(KEY_E)) state.gizmoOperation = ImGuizmo::ROTATE;
        if (IsKeyPressed(KEY_R)) state.gizmoOperation = ImGuizmo::SCALE;
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

void DrawViewport(StudioState& state) {
    if (!ImGui::Begin("Viewport", &state.showViewport)) { ImGui::End(); return; }
    ImGui::TextDisabled("Click to focus · WASD move · right mouse look · Space / Ctrl up or down · W / E / R transform");
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x > 1.0f && available.y > 1.0f) {
        state.requestedWidth = static_cast<int>(available.x);
        state.requestedHeight = static_cast<int>(available.y);
        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        const ImVec2 imageSize(available.x, available.y);
        ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(state.viewport.texture.id)),
                     imageSize, ImVec2(0, 1), ImVec2(1, 0));
        state.viewportHovered = ImGui::IsItemHovered();
        if (state.selectedEntity >= 0 && state.selectedEntity < static_cast<int>(state.map.entities.size()) &&
            !IsScriptObject(state.map.entities[static_cast<std::size_t>(state.selectedEntity)]) &&
            state.map.entities[static_cast<std::size_t>(state.selectedEntity)].kind != "screengui" &&
            state.map.entities[static_cast<std::size_t>(state.selectedEntity)].kind != "frame" &&
            state.map.entities[static_cast<std::size_t>(state.selectedEntity)].kind != "textlabel" &&
            state.map.entities[static_cast<std::size_t>(state.selectedEntity)].kind != "textbutton") {
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);
            ImGuizmo::SetOrthographic(false);
            const Matrix viewMatrix = MatrixLookAt(state.camera.position, state.camera.target, state.camera.up);
            const Matrix projectionMatrix = MatrixPerspective(state.camera.fovy * DEG2RAD,
                imageSize.x / imageSize.y, 0.01, 1000.0);
            auto view = ToColumnMajor(viewMatrix);
            auto projection = ToColumnMajor(projectionMatrix);
            auto& entity = state.map.entities[static_cast<std::size_t>(state.selectedEntity)];
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                if (ImGui::IsKeyPressed(ImGuiKey_W, false)) state.gizmoOperation = ImGuizmo::TRANSLATE;
                if (ImGui::IsKeyPressed(ImGuiKey_E, false)) state.gizmoOperation = ImGuizmo::ROTATE;
                if (ImGui::IsKeyPressed(ImGuiKey_R, false)) state.gizmoOperation = ImGuizmo::SCALE;
            }
            std::array<float, 3> translation = entity.transform.position;
            std::array<float, 3> rotation{};
            std::array<float, 3> scale = entity.transform.scale;
            for (std::size_t axis = 0; axis < rotation.size(); ++axis) {
                rotation[axis] = entity.transform.rotation[axis] * RAD2DEG;
            }
            std::array<float, 16> transform{};
            ImGuizmo::RecomposeMatrixFromComponents(translation.data(), rotation.data(), scale.data(), transform.data());
            if (ImGuizmo::Manipulate(view.data(), projection.data(), state.gizmoOperation,
                                     ImGuizmo::LOCAL, transform.data())) {
                ImGuizmo::DecomposeMatrixToComponents(transform.data(), translation.data(), rotation.data(), scale.data());
                entity.transform.position = translation;
                entity.transform.scale = scale;
                state.dirty = true;
                for (std::size_t axis = 0; axis < rotation.size(); ++axis) {
                    entity.transform.rotation[axis] = rotation[axis] * DEG2RAD;
                }
            }
        }
        if (state.viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
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
    ImGui::TextWrapped("Cloud actions are optional. Build first, then use Publish in the top bar. Local files are managed in File > Open and File > Save.");
    ImGui::TextDisabled("Website API: %s", state.api.baseUrl().c_str());
    ImGui::Text("Account: %s", state.displayName.c_str());
    ImGui::InputText("World ID", &state.cloudWorldId);
    ImGui::TextDisabled("The website session is managed by Studio and never included in map data.");
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
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(kInitialWidth, kInitialHeight, "Blubox Studio");
    if (!IsWindowReady()) throw std::runtime_error("Could not create the Blubox Studio window.");
    SetTargetFPS(60);
    state.viewport = LoadRenderTexture(1280, 720);
    SetTextureFilter(state.viewport.texture, TEXTURE_FILTER_BILINEAR);
    rlImGuiSetup(true);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    ApplyStudioTheme();
    LoadStudioFonts();

    while (!WindowShouldClose() && !state.exitRequested) {
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
        }
        UpdateViewportCamera(state, GetFrameTime());
        if (state.selectedEntity >= static_cast<int>(state.map.entities.size())) state.selectedEntity = -1;
        BeginDrawing();
        ClearBackground(Color{19, 19, 26, 255});
        if (state.authenticated && state.projectOpen) DrawWorld(state);
        rlImGuiBegin();
        if (!state.authenticated) {
            DrawLoginModal(state);
        } else if (state.projectOpen) {
            SetupDefaultDockLayout();
            ImGui::DockSpaceOverViewport(ImGui::GetID("BluboxStudioDockspace"), ImGui::GetMainViewport());
            HandleShortcuts(state);
            DrawTopBar(state);
            DrawNewMapConfirmation(state);
            DrawOpenMapConfirmation(state);
            DrawPublishDialog(state);
            if (state.authenticated && state.projectOpen) {
                if (state.showExplorer) DrawHierarchy(state);
                if (state.showProperties) DrawInspector(state);
                if (state.showScriptEditor) DrawScriptPanel(state);
                if (state.showViewport) DrawViewport(state);
                if (state.showOutput) DrawOutput(state);
                if (state.showWorldPanel) DrawWorldPanel(state);
                if (state.showCloudPanel) DrawCloudPanel(state);
            } else DrawLoginModal(state);
        }
        rlImGuiEnd();
        EndDrawing();
        EnsureViewport(state);
    }

    rlImGuiShutdown();
    if (state.viewport.texture.id != 0) UnloadRenderTexture(state.viewport);
    std::fill(state.websiteToken.begin(), state.websiteToken.end(), '\0');
    CloseWindow();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
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
#ifdef _WIN32
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        SetConsoleOutputCP(CP_UTF8);
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
#endif
        TraceLog(LOG_INFO, "Studio diagnostic console enabled; authentication tokens are redacted.");
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
        TraceLog(LOG_ERROR, "Blubox Studio: %s", exception.what());
        return 1;
    }
}
