#include "PublishedWorld.hpp"

#include "RaylibCompat.hpp"

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <raylib.h>
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define DrawText Win32DrawText
#define DrawTextEx Win32DrawTextEx
#define LoadImage Win32LoadImage
#define PlaySound Win32PlaySound
#include <windows.h>
#include <winhttp.h>
#undef PlaySound
#undef LoadImage
#undef DrawTextEx
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle
#endif

#include <algorithm>
#include <fstream>
#include <iterator>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

using Json = nlohmann::json;
constexpr std::size_t kMaxWorldBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaxWorldEntities = 100000;
constexpr std::size_t kMaxGuiDepth = 64;

float Number(const Json &object, const char *key, float fallback) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_number()) return fallback;
    const float value = object[key].get<float>();
    return std::isfinite(value) ? value : fallback;
}

bool Boolean(const Json &object, const char *key, bool fallback) {
    return object.is_object() && object.contains(key) && object[key].is_boolean()
        ? object[key].get<bool>() : fallback;
}

std::string String(const Json &object, const char *key, std::string fallback = {}) {
    return object.is_object() && object.contains(key) && object[key].is_string()
        ? object[key].get<std::string>() : std::move(fallback);
}

std::uint32_t NetworkIdForEntityId(std::string_view id) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char byte : id) { hash ^= byte; hash *= 16777619u; }
    return hash ? hash : 1u;
}

std::uint16_t ReadLe16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (std::uint16_t(p[1]) << 8));
}
std::uint32_t ReadLe32(const unsigned char* p) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= std::uint32_t(p[i]) << (i * 8);
    return value;
}

Vector2 Vector2Value(const Json &value, Vector2 fallback) {
    if (!value.is_array() || value.size() < 2 || !value[0].is_number() || !value[1].is_number()) return fallback;
    const float x = value[0].get<float>();
    const float y = value[1].get<float>();
    if (!std::isfinite(x) || !std::isfinite(y)) return fallback;
    return Vector2{std::clamp(x, -100000.0f, 100000.0f), std::clamp(y, -100000.0f, 100000.0f)};
}

Vector3 Vector3Value(const Json &value, Vector3 fallback) {
    if (!value.is_array() || value.size() < 3 || !value[0].is_number() ||
        !value[1].is_number() || !value[2].is_number()) return fallback;
    const float x = value[0].get<float>();
    const float y = value[1].get<float>();
    const float z = value[2].get<float>();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return fallback;
    return Vector3{std::clamp(x, -100000.0f, 100000.0f),
                   std::clamp(y, -100000.0f, 100000.0f),
                   std::clamp(z, -100000.0f, 100000.0f)};
}

Color ColorValue(const Json &value, Color fallback) {
    if (!value.is_array() || value.size() < 3) return fallback;
    for (std::size_t i = 0; i < std::min<std::size_t>(value.size(), 4); ++i) {
        if (!value[i].is_number()) return fallback;
    }
    return Color{static_cast<unsigned char>(std::clamp(value[0].get<int>(), 0, 255)),
                 static_cast<unsigned char>(std::clamp(value[1].get<int>(), 0, 255)),
                 static_cast<unsigned char>(std::clamp(value[2].get<int>(), 0, 255)),
                 static_cast<unsigned char>(value.size() > 3
                     ? std::clamp(value[3].get<int>(), 0, 255) : 255)};
}

Color WithTransparency(Color value, float transparency) {
    const float visible = 1.0f - std::clamp(transparency, 0.0f, 1.0f);
    value.a = static_cast<unsigned char>(std::lround(static_cast<float>(value.a) * visible));
    return value;
}

#if defined(_WIN32)
bool DownloadMap(std::wstring_view route, std::string &body, std::string &error) {
    HINTERNET session = WinHttpOpen(L"BluboxClient/World", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { error = "Could not initialize HTTPS for published world loading."; return false; }
    WinHttpSetTimeouts(session, 3000, 3000, 5000, 5000);
    HINTERNET connection = WinHttpConnect(session, L"blubox.runs-on.dev", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection
        ? WinHttpOpenRequest(connection, L"GET", std::wstring(route).c_str(), nullptr,
                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
        : nullptr;
    bool ok = false;
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                WINHTTP_NO_HEADER_INDEX) && status == 200) {
            std::string response;
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) break;
                if (available == 0) { body = std::move(response); ok = true; break; }
                if (response.size() + available > kMaxWorldBytes) {
                    error = "Published world exceeds the 4 MiB runtime limit.";
                    break;
                }
                const std::size_t oldSize = response.size();
                response.resize(oldSize + available);
                DWORD received = 0;
                if (!WinHttpReadData(request, response.data() + oldSize, available, &received)) break;
                response.resize(oldSize + received);
            }
        } else if (status == 404) {
            error = "This game has no published Blubox Studio map yet.";
        } else {
            error = "The website could not provide this published world.";
        }
    } else {
        error = "Could not connect to the published world service.";
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}
#endif

std::vector<std::string> WrapText(const Font *font, const std::string &text, float fontSize,
                                  float maxWidth, float maxHeight, bool wrap, bool truncate) {
    const auto measure = [font, fontSize](const std::string &line) {
        if (!font) return static_cast<float>(MeasureText(line.c_str(), static_cast<int>(fontSize)));
        return MeasureTextExSafe(font, line.c_str(), fontSize, 1.0f).x;
    };
    if (!wrap) {
        std::string line = text;
        if (truncate && measure(line) > maxWidth) {
            while (!line.empty() && measure(line + "...") > maxWidth) line.pop_back();
            line += "...";
        }
        return {std::move(line)};
    }
    std::vector<std::string> lines;
    std::string line;
    std::string word;
    auto addWord = [&] {
        if (word.empty()) return;
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(candidate) > maxWidth) {
            lines.push_back(std::move(line));
            line = std::move(word);
        } else line = std::move(candidate);
        word.clear();
    };
    for (const unsigned char ch : text) {
        if (std::isspace(ch)) addWord();
        else word.push_back(static_cast<char>(ch));
    }
    addWord();
    if (!line.empty() || lines.empty()) lines.push_back(std::move(line));
    if (truncate) {
        const std::size_t maxLines = std::max<std::size_t>(1, static_cast<std::size_t>(
            std::max(1.0f, maxHeight / std::max(1.0f, fontSize * 1.2f))));
        if (lines.size() > maxLines) lines.resize(maxLines);
        if (!lines.empty()) {
            std::string &last = lines.back();
            while (!last.empty() && measure(last + "...") > maxWidth) last.pop_back();
            last += "...";
        }
    }
    return lines;
}

} // namespace

bool PublishedWorld::Load(std::string_view worldId, std::string &error) {
    parts_.clear();
    gui_.clear();
    guiIndex_.clear();
    localScripts_.clear();
    screenGuis_.clear();
    capturedButton_.reset();
    spawnPosition_ = Vector3{0.0f, 3.0f, 0.0f};
    if (worldId.empty() || worldId.size() > 64 ||
        !std::all_of(worldId.begin(), worldId.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_';
        })) {
        error = "The join link has an invalid world id.";
        return false;
    }

#if defined(_WIN32)
    std::wstring route = L"/api/client/world/";
    route.append(worldId.begin(), worldId.end());
    std::string payload;
    if (!DownloadMap(route, payload, error)) return false;
#else
    error = "Published world loading is only available on Windows.";
    return false;
#endif

    try {
        const Json response = Json::parse(payload);
        const Json map = response.at("map");
        if (!map.is_object() || map.value("format", std::string{}) != "blubox-map" ||
            map.value("formatVersion", 0) != 1 || !map.contains("entities") ||
            !map["entities"].is_array() || map["entities"].size() > kMaxWorldEntities) {
            error = "The published world uses an unsupported map format.";
            return false;
        }

        std::unordered_map<std::string, std::string> entityParents;
        const Json &entities = map["entities"];
        gui_.reserve(entities.size());
        parts_.reserve(entities.size());
        for (const Json &entity : entities) {
            if (!entity.is_object()) continue;
            const std::string kind = String(entity, "kind");
            const std::string id = String(entity, "id");
            entityParents.emplace(id, String(entity, "parent", "Workspace"));
            const Json properties = entity.value("properties", Json::object());
            if (kind == "screengui") {
                GuiElement element;
                element.id = id;
                element.name = String(entity, "name", "ScreenGui");
                element.parent = String(entity, "parent");
                element.kind = kind;
                element.enabled = Boolean(properties, "enabled", true);
                element.visible = Boolean(properties, "visible", true);
                guiIndex_.emplace(element.id, gui_.size());
                gui_.push_back(std::move(element));
                continue;
            }
            if (kind == "frame" || kind == "textlabel" || kind == "textbutton") {
                GuiElement element;
                element.id = id;
                element.name = String(entity, "name", kind == "textbutton" ? "TextButton" :
                                                               kind == "textlabel" ? "TextLabel" : "Frame");
                element.parent = String(entity, "parent");
                element.kind = kind;
                element.text = String(properties, "text");
                element.horizontalAlignment = String(properties, "horizontalAlignment", "Left");
                element.verticalAlignment = String(properties, "verticalAlignment", "Top");
                element.textXAlignment = String(properties, "textXAlignment", "Left");
                element.textYAlignment = String(properties, "textYAlignment", "Center");
                element.position = Vector2Value(properties.value("position", Json::array()), Vector2{40.0f, 40.0f});
                element.positionScale = Vector2Value(properties.value("positionScale", Json::array()), Vector2{});
                element.size = Vector2Value(properties.value("size", Json::array()), Vector2{220.0f, 64.0f});
                element.sizeScale = Vector2Value(properties.value("sizeScale", Json::array()), Vector2{});
                element.background = ColorValue(properties.value("backgroundColor", Json::array()), element.background);
                element.textColor = ColorValue(properties.value("textColor", Json::array()), element.textColor);
                element.backgroundTransparency = std::clamp(Number(properties, "backgroundTransparency", 0.0f), 0.0f, 1.0f);
                element.textTransparency = std::clamp(Number(properties, "textTransparency", 0.0f), 0.0f, 1.0f);
                element.fontSize = std::clamp(Number(properties, "fontSize", 20.0f), 6.0f, 144.0f);
                element.cornerRadius = std::clamp(Number(properties, "cornerRadius", 0.0f), 0.0f, 256.0f);
                element.scaleWithViewport = Boolean(properties, "scaleWithViewport", true);
                element.textScaled = Boolean(properties, "textScaled", false);
                element.textWrapped = Boolean(properties, "textWrapped", false);
                element.textTruncate = Boolean(properties, "textTruncate", false);
                element.visible = Boolean(properties, "visible", true) && Boolean(entity, "visible", true);
                element.zIndex = std::clamp(static_cast<int>(Number(properties, "zIndex", 1.0f)), 0, 100000);
                guiIndex_.emplace(element.id, gui_.size());
                gui_.push_back(std::move(element));
                continue;
            }

            if (kind == "block" || kind == "spawnlocation" || kind == "sphere" ||
                kind == "wedgepart" || kind == "cornerwedgepart") {
                const Json transform = entity.value("transform", Json::object());
                const Vector3 position = Vector3Value(transform.value("position", Json::array()), Vector3{});
                const Vector3 size = Vector3Value(transform.value("scale", Json::array()), Vector3{1.0f, 1.0f, 1.0f});
                const Color color = ColorValue(entity.value("color", Json::array()), Color{75, 163, 226, 255});
                const float transparency = std::clamp(Number(properties, "transparency", 0.0f), 0.0f, 1.0f);
                parts_.push_back(StaticPart{kind, position, size, WithTransparency(color, transparency),
                                             Boolean(entity, "visible", true), Boolean(properties, "Anchored", true)});
                parts_.back().networkId = NetworkIdForEntityId(id);
                if (kind == "spawnlocation" && Boolean(properties, "Enabled", true)) {
                    spawnPosition_ = Vector3{position.x, position.y + std::max(0.6f, size.y * 0.5f + 1.1f), position.z};
                }
            }
        }

        if (map.contains("scriptObjects") && map["scriptObjects"].is_array()) {
            const Json &scripts = map["scriptObjects"];
            if (scripts.size() > 4096) throw std::runtime_error("The published map has too many script objects.");
            std::size_t scriptBytes = 0;
            for (const Json &script : scripts) {
                if (!script.is_object() || String(script, "className") != "LocalScript") continue;
                LocalScript local;
                local.id = String(script, "id");
                local.name = String(script, "name", "LocalScript");
                local.parent = String(script, "parent");
                local.source = String(script, "source");
                if (local.id.empty() || local.id.size() >= 128 || local.parent.size() >= 128 ||
                    local.source.empty() || local.source.size() > 256u * 1024u ||
                    scriptBytes > 2u * 1024u * 1024u - local.source.size()) continue;
                bool clientContext = false;
                std::string parent = local.parent;
                for (std::size_t depth = 0; depth <= entities.size(); ++depth) {
                    if (parent == "StarterGui" || parent == "StarterPlayer") {
                        clientContext = true;
                        break;
                    }
                    const auto found = entityParents.find(parent);
                    if (found == entityParents.end() || found->second == parent) break;
                    parent = found->second;
                }
                if (!clientContext) continue;
                scriptBytes += local.source.size();
                localScripts_.push_back(std::move(local));
            }
        }

        for (std::size_t i = 0; i < gui_.size(); ++i) {
            const auto parent = guiIndex_.find(gui_[i].parent);
            if (parent != guiIndex_.end() && parent->second != i) {
                gui_[i].parentIndex = parent->second;
                gui_[parent->second].children.push_back(i);
            }
            else if (gui_[i].kind == "screengui" && gui_[i].parent == "StarterGui" && gui_[i].enabled)
                screenGuis_.push_back(i);
        }
        for (auto &element : gui_) {
            std::stable_sort(element.children.begin(), element.children.end(), [this](std::size_t a, std::size_t b) {
                return gui_[a].zIndex < gui_[b].zIndex;
            });
        }
    } catch (const std::exception &exception) {
        parts_.clear();
        gui_.clear();
        guiIndex_.clear();
        localScripts_.clear();
        screenGuis_.clear();
        error = std::string("Could not read the published world: ") + exception.what();
        return false;
    }
    return true;
}

bool PublishedWorld::LoadLocalMapFile(std::string_view path, std::string &error) {
    parts_.clear(); gui_.clear(); guiIndex_.clear(); localScripts_.clear(); screenGuis_.clear();
    capturedButton_.reset(); spawnPosition_ = Vector3{0.0f, 3.0f, 0.0f};
    if (path.empty() || path.size() > 32760) { error = "Invalid Studio playtest map path."; return false; }
    std::ifstream file(std::string(path), std::ios::binary);
    if (!file) { error = "Studio playtest map could not be opened."; return false; }
    std::string payload((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (payload.empty() || payload.size() > 64u * 1024u * 1024u) { error = "Studio playtest map has an invalid size."; return false; }
    try {
        const Json map = Json::parse(payload);
        if (!map.is_object() || map.value("format", std::string{}) != "blubox-map" ||
            map.value("formatVersion", 0) != 1 || !map.contains("entities") ||
            !map["entities"].is_array() || map["entities"].size() > kMaxWorldEntities) {
            error = "Studio supplied an unsupported Blubox map."; return false;
        }
        const Json& entities = map["entities"];
        std::unordered_map<std::string, std::string> parents;
        for (const Json& entity : entities) {
            if (!entity.is_object()) continue;
            const std::string kind = String(entity, "kind");
            const std::string id = String(entity, "id");
            parents[id] = String(entity, "parent", "Workspace");
            const Json properties = entity.value("properties", Json::object());
            if (Boolean(properties, "RuntimeVisualOnly", false)) continue;
            if (kind == "screengui" || kind == "frame" || kind == "textlabel" || kind == "textbutton") {
                GuiElement element;
                element.id = id;
                element.name = String(entity, "name", kind == "screengui" ? "ScreenGui" : kind == "textbutton" ? "TextButton" : kind == "textlabel" ? "TextLabel" : "Frame");
                element.parent = String(entity, "parent"); element.kind = kind;
                element.enabled = Boolean(properties, "enabled", true);
                element.visible = Boolean(properties, "visible", true) && Boolean(entity, "visible", true);
                element.text = String(properties, "text");
                element.horizontalAlignment = String(properties, "horizontalAlignment", "Left");
                element.verticalAlignment = String(properties, "verticalAlignment", "Top");
                element.textXAlignment = String(properties, "textXAlignment", "Left");
                element.textYAlignment = String(properties, "textYAlignment", "Center");
                element.position = Vector2Value(properties.value("position", Json::array()), Vector2{40, 40});
                element.positionScale = Vector2Value(properties.value("positionScale", Json::array()), Vector2{});
                element.size = Vector2Value(properties.value("size", Json::array()), Vector2{220, 64});
                element.sizeScale = Vector2Value(properties.value("sizeScale", Json::array()), Vector2{});
                element.background = ColorValue(properties.value("backgroundColor", Json::array()), element.background);
                element.textColor = ColorValue(properties.value("textColor", Json::array()), element.textColor);
                element.backgroundTransparency = std::clamp(Number(properties, "backgroundTransparency", 0), 0.0f, 1.0f);
                element.textTransparency = std::clamp(Number(properties, "textTransparency", 0), 0.0f, 1.0f);
                element.fontSize = std::clamp(Number(properties, "fontSize", 20), 6.0f, 144.0f);
                element.cornerRadius = std::clamp(Number(properties, "cornerRadius", 0), 0.0f, 256.0f);
                element.scaleWithViewport = Boolean(properties, "scaleWithViewport", true);
                element.textScaled = Boolean(properties, "textScaled", false);
                element.textWrapped = Boolean(properties, "textWrapped", false);
                element.textTruncate = Boolean(properties, "textTruncate", false);
                element.zIndex = std::clamp(static_cast<int>(Number(properties, "zIndex", 1)), 0, 100000);
                guiIndex_[element.id] = gui_.size(); gui_.push_back(std::move(element));
            } else if (kind == "block" || kind == "spawnlocation" || kind == "sphere" || kind == "wedgepart" || kind == "cornerwedgepart") {
                const Json transform = entity.value("transform", Json::object());
                const Vector3 pos = Vector3Value(transform.value("position", Json::array()), Vector3{});
                const Vector3 size = Vector3Value(transform.value("scale", Json::array()), Vector3{1,1,1});
                const Color color = ColorValue(entity.value("color", Json::array()), Color{75,163,226,255});
                parts_.push_back({kind, pos, size, WithTransparency(color, std::clamp(Number(properties,"transparency",0),0.0f,1.0f)), Boolean(entity,"visible",true), Boolean(properties,"Anchored",true)});
                parts_.back().networkId = NetworkIdForEntityId(id);
                if (kind == "spawnlocation" && Boolean(properties,"Enabled",true)) spawnPosition_ = {pos.x,pos.y+std::max(0.6f,size.y*0.5f+1.1f),pos.z};
            }
        }
        if (map.contains("scriptObjects") && map["scriptObjects"].is_array()) for (const Json& script : map["scriptObjects"]) {
            if (!script.is_object() || String(script,"className") != "LocalScript") continue;
            LocalScript item{String(script,"id"), String(script,"name","LocalScript"), String(script,"parent"), String(script,"source")};
            std::string parent = item.parent; bool clientContext = false;
            for (std::size_t depth=0; depth<=entities.size(); ++depth) {
                if (parent == "StarterGui" || parent == "StarterPlayer") { clientContext=true; break; }
                const auto found=parents.find(parent); if(found==parents.end() || found->second==parent) break; parent=found->second;
            }
            if(clientContext && !item.source.empty() && item.source.size()<=256u*1024u) localScripts_.push_back(std::move(item));
        }
        for (std::size_t i=0;i<gui_.size();++i) {
            const auto parent=guiIndex_.find(gui_[i].parent);
            if(parent!=guiIndex_.end() && parent->second!=i) { gui_[i].parentIndex=parent->second; gui_[parent->second].children.push_back(i); }
            else if(gui_[i].kind=="screengui" && gui_[i].parent=="StarterGui" && gui_[i].enabled) screenGuis_.push_back(i);
        }
        for(auto& element:gui_) std::stable_sort(element.children.begin(),element.children.end(),[this](std::size_t a,std::size_t b){return gui_[a].zIndex<gui_[b].zIndex;});
    } catch (const std::exception& exception) {
        parts_.clear(); gui_.clear(); guiIndex_.clear(); localScripts_.clear(); screenGuis_.clear();
        error = std::string("Could not read Studio's local map: ") + exception.what(); return false;
    }
    error.clear(); return true;
}

bool PublishedWorld::ApplyStudioPropertyDelta(std::span<const unsigned char> payload) {
    // Payload starts after the RemoteManager session token: timestamp:u64,
    // count:u16, then fixed-size property records.
    if (payload.size() < 10) return false;
    const std::uint16_t count = ReadLe16(payload.data() + 8);
    constexpr std::size_t kRecordSize = 32;
    if (count > 300 || payload.size() != 10u + std::size_t(count) * kRecordSize) return false;
    const unsigned char* record = payload.data() + 10;
    for (std::uint16_t i = 0; i < count; ++i, record += kRecordSize) {
        const std::uint32_t networkId = ReadLe32(record);
        const std::uint16_t mask = ReadLe16(record + 8);
        const auto found = std::find_if(parts_.begin(), parts_.end(), [networkId](const StaticPart& part) {
            return part.networkId == networkId;
        });
        if (found == parts_.end()) continue;
        const unsigned char* transform = record + 10;
        const auto decode = [](std::uint16_t value, float low, float high) {
            return low + (static_cast<float>(value) / 65535.0f) * (high - low);
        };
        if (mask & 1u) {
            found->position = {decode(ReadLe16(transform), -4096.0f, 4096.0f),
                               decode(ReadLe16(transform + 2), -4096.0f, 4096.0f),
                               decode(ReadLe16(transform + 4), -4096.0f, 4096.0f)};
            found->size = {decode(ReadLe16(transform + 6), 0.0f, 64.0f),
                           decode(ReadLe16(transform + 8), 0.0f, 64.0f),
                           decode(ReadLe16(transform + 10), 0.0f, 64.0f)};
        }
        if (mask & 2u) found->color = {record[22], record[23], record[24], 255};
    }
    return true;
}

namespace {
std::optional<std::size_t> GuiIndex(const std::unordered_map<std::string, std::size_t> &indices,
                                   std::string_view id) {
    const auto found = indices.find(std::string(id));
    return found == indices.end() ? std::nullopt : std::optional<std::size_t>(found->second);
}
}

std::optional<std::string> PublishedWorld::GuiParent(std::string_view id) const {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size()) return std::nullopt;
    return gui_[*index].parent;
}

std::optional<std::string> PublishedWorld::GuiName(std::string_view id) const {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size()) return std::nullopt;
    return gui_[*index].name;
}

std::optional<std::string> PublishedWorld::GuiText(std::string_view id) const {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size()) return std::nullopt;
    return gui_[*index].text;
}

std::optional<bool> PublishedWorld::GuiVisible(std::string_view id) const {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size()) return std::nullopt;
    return gui_[*index].visible;
}

bool PublishedWorld::SetGuiParent(std::string_view id, std::string_view parentId) {
    const auto target = GuiIndex(guiIndex_, id);
    if (!target || *target >= gui_.size() || parentId.size() >= 128) return false;
    const bool root = parentId == "PlayerGui" || parentId == "StarterGui";
    const auto parent = GuiIndex(guiIndex_, parentId);
    const bool detached = parentId.empty();
    if (!root && !parent && !detached) return false;
    if (parent && *parent == *target) return false;
    std::size_t current = parent.value_or(gui_.size());
    for (std::size_t depth = 0; current < gui_.size() && depth <= gui_.size(); ++depth) {
        if (current == *target) return false;
        const auto next = GuiIndex(guiIndex_, gui_[current].parent);
        current = next.value_or(gui_.size());
    }
    if (const auto oldParent = GuiIndex(guiIndex_, gui_[*target].parent); oldParent && *oldParent < gui_.size()) {
        auto &siblings = gui_[*oldParent].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), *target), siblings.end());
    }
    gui_[*target].parent.assign(parentId);
    gui_[*target].parentIndex = parent.value_or(std::numeric_limits<std::size_t>::max());
    if (parent) gui_[*parent].children.push_back(*target);
    screenGuis_.erase(std::remove(screenGuis_.begin(), screenGuis_.end(), *target), screenGuis_.end());
    if (root && gui_[*target].enabled) screenGuis_.push_back(*target);
    return true;
}

bool PublishedWorld::SetGuiName(std::string_view id, std::string_view value) {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size() || value.empty() || value.size() > 128) return false;
    gui_[*index].name.assign(value);
    return true;
}

bool PublishedWorld::SetGuiText(std::string_view id, std::string_view value) {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size() || value.size() > 4096) return false;
    gui_[*index].text.assign(value);
    return true;
}

bool PublishedWorld::SetGuiVisible(std::string_view id, bool value) {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size()) return false;
    gui_[*index].visible = value;
    return true;
}

bool PublishedWorld::SetGuiTransparency(std::string_view id, std::string_view property, float value) {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size() || !std::isfinite(value)) return false;
    const float transparency = std::clamp(value, 0.0f, 1.0f);
    if (property == "BackgroundTransparency") gui_[*index].backgroundTransparency = transparency;
    else if (property == "TextTransparency") gui_[*index].textTransparency = transparency;
    else return false;
    return true;
}

std::optional<float> PublishedWorld::GuiTransparency(std::string_view id, std::string_view property) const {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size()) return std::nullopt;
    if (property == "BackgroundTransparency") return gui_[*index].backgroundTransparency;
    if (property == "TextTransparency") return gui_[*index].textTransparency;
    return std::nullopt;
}

bool PublishedWorld::SetGuiColor(std::string_view id, std::string_view property, Color value) {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size()) return false;
    if (property == "BackgroundColor3") gui_[*index].background = value;
    else if (property == "TextColor3") gui_[*index].textColor = value;
    else return false;
    return true;
}

std::optional<Color> PublishedWorld::GuiColor(std::string_view id, std::string_view property) const {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size()) return std::nullopt;
    if (property == "BackgroundColor3") return gui_[*index].background;
    if (property == "TextColor3") return gui_[*index].textColor;
    return std::nullopt;
}

bool PublishedWorld::SetGuiLayout(std::string_view id, std::string_view property,
                                  Vector2 offset, Vector2 scale) {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size() || !std::isfinite(offset.x) || !std::isfinite(offset.y) ||
        !std::isfinite(scale.x) || !std::isfinite(scale.y)) return false;
    GuiElement &element = gui_[*index];
    if (property == "Position") { element.position = offset; element.positionScale = scale; }
    else if (property == "Size") { element.size = offset; element.sizeScale = scale; }
    else return false;
    return true;
}

bool PublishedWorld::GetGuiLayout(std::string_view id, std::string_view property,
                                  Vector2 &offset, Vector2 &scale) const {
    const auto index = GuiIndex(guiIndex_, id);
    if (!index || *index >= gui_.size()) return false;
    const GuiElement &element = gui_[*index];
    if (property == "Position") { offset = element.position; scale = element.positionScale; }
    else if (property == "Size") { offset = element.size; scale = element.sizeScale; }
    else return false;
    return true;
}

std::optional<std::string> PublishedWorld::FindGuiChild(std::string_view parentId,
                                                        std::string_view childName) const {
    if (parentId == "PlayerGui" || parentId == "StarterGui") {
        for (const std::size_t index : screenGuis_) {
            if (index < gui_.size() && gui_[index].name == childName) return gui_[index].id;
        }
        return std::nullopt;
    }
    const auto parent = GuiIndex(guiIndex_, parentId);
    if (!parent || *parent >= gui_.size()) return std::nullopt;
    for (const std::size_t index : gui_[*parent].children) {
        if (index < gui_.size() && gui_[index].name == childName) return gui_[index].id;
    }
    return std::nullopt;
}

std::string PublishedWorld::CreateGui(std::string_view className) {
    const std::string kind = className == "ScreenGui" ? "screengui" :
        className == "Frame" ? "frame" : className == "TextLabel" ? "textlabel" :
        className == "TextButton" ? "textbutton" : std::string{};
    if (kind.empty() || gui_.size() >= kMaxWorldEntities) return {};
    GuiElement element;
    element.id = "client-ui-" + std::to_string(nextLocalGuiId_++);
    element.name = std::string(className);
    element.kind = kind;
    element.parent.clear();
    element.size = Vector2{220.0f, 64.0f};
    if (kind == "screengui") { element.size = Vector2{}; element.visible = true; }
    const std::size_t index = gui_.size();
    guiIndex_.emplace(element.id, index);
    gui_.push_back(std::move(element));
    return gui_[index].id;
}

bool PublishedWorld::HandleGuiPointer(Vector2 pointer, bool pressed, bool released,
                                      std::string &activatedId) {
    activatedId.clear();
    std::vector<std::size_t> ordered;
    const auto visit = [this, &ordered](auto &&self, std::size_t parent, std::size_t depth) -> void {
        if (depth > kMaxGuiDepth || parent >= gui_.size()) return;
        for (const std::size_t child : gui_[parent].children) {
            if (child >= gui_.size()) continue;
            ordered.push_back(child);
            self(self, child, depth + 1);
        }
    };
    for (const std::size_t root : screenGuis_) {
        if (root < gui_.size() && gui_[root].kind != "screengui") ordered.push_back(root);
        visit(visit, root, 0);
    }
    for (std::size_t reverse = ordered.size(); reverse > 0; --reverse) {
        const std::size_t index = ordered[reverse - 1];
        const GuiElement &element = gui_[index];
        if (element.kind != "textbutton" || !element.visible) continue;
        bool ancestorsVisible = true;
        std::size_t ancestor = index;
        for (std::size_t depth = 0; depth <= kMaxGuiDepth; ++depth) {
            const auto parent = GuiIndex(guiIndex_, gui_[ancestor].parent);
            if (!parent) break;
            ancestor = *parent;
            if (ancestor >= gui_.size() || !gui_[ancestor].visible) { ancestorsVisible = false; break; }
        }
        if (!ancestorsVisible) continue;
        const Rect rect = ResolveGuiRect(index, static_cast<float>(GetScreenWidth()),
                                         static_cast<float>(GetScreenHeight()), 0);
        if (pointer.x >= rect.x && pointer.y >= rect.y &&
            pointer.x <= rect.x + rect.width && pointer.y <= rect.y + rect.height) {
            if (pressed) capturedButton_ = index;
            if (released && capturedButton_ && *capturedButton_ == index) activatedId = element.id;
            if (pressed || capturedButton_ == index) {
                if (released) capturedButton_.reset();
                return true;
            }
        }
    }
    if (released && capturedButton_) {
        const std::size_t target = *capturedButton_;
        if (target < gui_.size()) {
            const Rect rect = ResolveGuiRect(target, static_cast<float>(GetScreenWidth()),
                                             static_cast<float>(GetScreenHeight()), 0);
            if (pointer.x >= rect.x && pointer.y >= rect.y &&
                pointer.x <= rect.x + rect.width && pointer.y <= rect.y + rect.height)
                activatedId = gui_[target].id;
        }
        capturedButton_.reset();
        return true;
    }
    return false;
}

PublishedWorld::Rect PublishedWorld::ResolveGuiRect(std::size_t index, float viewportWidth,
                                                     float viewportHeight, std::size_t depth) const {
    if (index >= gui_.size() || depth >= kMaxGuiDepth) return {};
    const GuiElement &element = gui_[index];
    Rect parent{0.0f, 0.0f, viewportWidth, viewportHeight};
    if (element.parentIndex < gui_.size()) {
        const GuiElement &parentElement = gui_[element.parentIndex];
        if (parentElement.kind == "screengui") {
            if ((parentElement.parent != "StarterGui" && parentElement.parent != "PlayerGui") || !parentElement.enabled) return {};
        } else {
            parent = ResolveGuiRect(element.parentIndex, viewportWidth, viewportHeight, depth + 1);
            if (!parentElement.visible || parent.width <= 0.0f || parent.height <= 0.0f) return {};
        }
    } else if (element.parent != "PlayerGui" && element.parent != "StarterGui") {
        return {};
    }

    const float scaleX = element.scaleWithViewport ? viewportWidth / 1280.0f : 1.0f;
    const float scaleY = element.scaleWithViewport ? viewportHeight / 720.0f : 1.0f;
    const float width = std::clamp(element.size.x, 8.0f, 100000.0f) * scaleX +
        std::clamp(element.sizeScale.x, 0.0f, 10.0f) * parent.width;
    const float height = std::clamp(element.size.y, 8.0f, 100000.0f) * scaleY +
        std::clamp(element.sizeScale.y, 0.0f, 10.0f) * parent.height;
    float x = std::clamp(element.position.x, -100000.0f, 100000.0f) * scaleX +
        std::clamp(element.positionScale.x, -10.0f, 10.0f) * parent.width;
    float y = std::clamp(element.position.y, -100000.0f, 100000.0f) * scaleY +
        std::clamp(element.positionScale.y, -10.0f, 10.0f) * parent.height;
    const float resolvedWidth = std::clamp(width, 8.0f, 400000.0f);
    const float resolvedHeight = std::clamp(height, 8.0f, 400000.0f);
    if (element.horizontalAlignment == "Center") x += (parent.width - resolvedWidth) * 0.5f;
    else if (element.horizontalAlignment == "Right") x += parent.width - resolvedWidth;
    if (element.verticalAlignment == "Center") y += (parent.height - resolvedHeight) * 0.5f;
    else if (element.verticalAlignment == "Bottom") y += parent.height - resolvedHeight;
    return Rect{parent.x + x, parent.y + y, resolvedWidth, resolvedHeight};
}

void PublishedWorld::DrawGuiChildren(std::size_t parentIndex, int screenWidth, int screenHeight,
                                      const Font *font, std::size_t depth) const {
    if (depth >= kMaxGuiDepth || parentIndex >= gui_.size()) return;
    for (const std::size_t index : gui_[parentIndex].children)
        DrawGuiElement(index, screenWidth, screenHeight, font, depth + 1);
}

void PublishedWorld::DrawGuiElement(std::size_t index, int screenWidth, int screenHeight,
                                    const Font *font, std::size_t depth) const {
    if (depth >= kMaxGuiDepth || index >= gui_.size()) return;
    const GuiElement &element = gui_[index];
    if (!element.visible) return;
    const Rect rect = ResolveGuiRect(index, static_cast<float>(screenWidth),
                                     static_cast<float>(screenHeight), 0);
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;
    const int x = static_cast<int>(std::lround(rect.x));
    const int y = static_cast<int>(std::lround(rect.y));
    const int width = std::max(1, static_cast<int>(std::lround(rect.width)));
    const int height = std::max(1, static_cast<int>(std::lround(rect.height)));
    const Color background = WithTransparency(element.background, element.backgroundTransparency);
    if (background.a > 0) DrawRectangle(x, y, width, height, background);

    if ((element.kind == "textlabel" || element.kind == "textbutton") && !element.text.empty()) {
        const float baseScale = element.scaleWithViewport
            ? std::min(static_cast<float>(screenWidth) / 1280.0f,
                       static_cast<float>(screenHeight) / 720.0f) : 1.0f;
        float fontSize = std::clamp(element.fontSize * baseScale, 6.0f, 144.0f);
        const float insetX = std::min(10.0f, static_cast<float>(width) * 0.08f);
        const float insetY = std::min(4.0f, static_cast<float>(height) * 0.08f);
        const float textWidth = std::max(1.0f, static_cast<float>(width) - insetX * 2.0f);
        const float textHeight = std::max(1.0f, static_cast<float>(height) - insetY * 2.0f);
        auto lines = WrapText(font, element.text, fontSize, textWidth, textHeight,
                              element.textWrapped, element.textTruncate);
        const auto measure = [font](const std::string &line, float size) {
            return font ? MeasureTextExSafe(font, line.c_str(), size, 1.0f)
                        : Vector2{static_cast<float>(MeasureText(line.c_str(), static_cast<int>(size))), size};
        };
        if (element.textScaled && !element.text.empty()) {
            float widest = 1.0f;
            for (const auto &line : lines) widest = std::max(widest, measure(line, fontSize).x);
            const float contentHeight = std::max(1.0f, fontSize * 1.2f * static_cast<float>(lines.size()));
            const float fit = std::min(textWidth / widest, textHeight / contentHeight);
            fontSize = std::clamp(fontSize * fit, 6.0f, 144.0f);
            lines = WrapText(font, element.text, fontSize, textWidth, textHeight,
                             element.textWrapped, element.textTruncate);
        }
        const float lineHeight = fontSize * 1.2f;
        const float totalHeight = lineHeight * static_cast<float>(lines.size());
        float textY = static_cast<float>(y) + insetY;
        if (element.textYAlignment == "Center") textY = static_cast<float>(y) + (static_cast<float>(height) - totalHeight) * 0.5f;
        else if (element.textYAlignment == "Bottom") textY = static_cast<float>(y + height) - totalHeight - insetY;
        const Color textColor = WithTransparency(element.textColor, element.textTransparency);
        BeginScissorMode(x, y, width, height);
        for (const auto &line : lines) {
            const float lineWidth = measure(line, fontSize).x;
            float textX = static_cast<float>(x) + insetX;
            if (element.textXAlignment == "Center") textX = static_cast<float>(x) + (static_cast<float>(width) - lineWidth) * 0.5f;
            else if (element.textXAlignment == "Right") textX = static_cast<float>(x + width) - lineWidth - insetX;
            if (font) DrawTextExSafe(font, line.c_str(), Vector2{textX, textY}, fontSize, 1.0f, textColor);
            else DrawText(line.c_str(), static_cast<int>(textX), static_cast<int>(textY), static_cast<int>(fontSize), textColor);
            textY += lineHeight;
        }
        EndScissorMode();
    }
    DrawGuiChildren(index, screenWidth, screenHeight, font, depth + 1);
}

void PublishedWorld::DrawGui(int screenWidth, int screenHeight, const Font *font) const {
    if (screenWidth <= 0 || screenHeight <= 0) return;
    for (const std::size_t index : screenGuis_) {
        if (index < gui_.size() && gui_[index].kind == "screengui")
            DrawGuiChildren(index, screenWidth, screenHeight, font, 0);
        else
            DrawGuiElement(index, screenWidth, screenHeight, font, 0);
    }
}
