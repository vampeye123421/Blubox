#include "Launcher.hpp"
#include "BluboxLauncherFont.hpp"
#include "BluboxBrandIcon.hpp"
#include "WindowsProtocolRegistration.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#endif

#include <raylib.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr char kManifestUrl[] =
    "https://raw.githubusercontent.com/vampeye123421/Blubox/main/version.json";

struct UpdateManifest {
    std::string version;
    std::string downloadUrl;
    std::string sha256;
    std::string launcherVersion;
    std::string launcherDownloadUrl;
    std::string launcherSha256;
    std::string studioVersion;
    std::string studioDownloadUrl;
    std::string studioSha256;
    std::string studioFontUrl;
    std::string studioFontSha256;
    std::string studioCodeFontUrl;
    std::string studioCodeFontSha256;
    std::string studioLauncherDownloadUrl;
    std::string studioLauncherSha256;
    std::string studioLauncherVersion;
};

struct SplashState {
    Font font{};
    Texture2D icon{};
    std::chrono::steady_clock::time_point transferStarted{};
    bool ownsFont = false;
    bool dragging = false;
    bool cancelRequested = false;
    bool darkMode = false;
};

SplashState g_splash;

void DrawProgress(float progress, std::string_view status, std::uint64_t totalBytes = 0,
                  std::uint64_t transferredBytes = 0, double bytesPerSecond = 0.0);

#ifdef _WIN32
struct WinHttpHandle {
    HINTERNET value = nullptr;
    ~WinHttpHandle() { if (value != nullptr) WinHttpCloseHandle(value); }
    WinHttpHandle(const WinHttpHandle &) = delete;
    WinHttpHandle &operator=(const WinHttpHandle &) = delete;
    WinHttpHandle() = default;
};

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) throw std::runtime_error("The update URL is not valid UTF-8.");
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::string AddManifestCacheBuster(std::string url) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    url += url.find('?') == std::string::npos ? "?" : "&";
    url += "blubox_check=" + std::to_string(now);
    return url;
}

struct UrlParts {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    DWORD flags = 0;
};

UrlParts CrackHttpsUrl(std::string_view url) {
    const std::wstring wideUrl = Utf8ToWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS) {
        throw std::runtime_error("The manifest must use a valid HTTPS URL.");
    }
    UrlParts result;
    result.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    result.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) result.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (result.path.empty()) result.path = L"/";
    result.port = parts.nPort;
    result.flags = WINHTTP_FLAG_SECURE;
    return result;
}

template <typename Consumer>
void TransferHttps(const UrlParts &url, Consumer &&consume) {
    WinHttpHandle session;
    session.value = WinHttpOpen(L"BluboxLauncher/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.value) throw std::runtime_error("WinHTTP is unavailable.");
    WinHttpSetTimeouts(session.value, 8000, 8000, 15000, 15000);
    WinHttpHandle connection;
    connection.value = WinHttpConnect(session.value, url.host.c_str(), url.port, 0);
    if (!connection.value) throw std::runtime_error("Could not connect to the update feed over HTTPS.");
    WinHttpHandle request;
    request.value = WinHttpOpenRequest(connection.value, L"GET", url.path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.flags);
    if (!request.value) throw std::runtime_error("The HTTPS request could not be created.");
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        throw std::runtime_error("The HTTPS download failed.");
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX) || status != 200) {
        throw std::runtime_error("The update server returned HTTP " + std::to_string(status) + ".");
    }

    ULONGLONG total = 0;
    wchar_t lengthText[32]{};
    DWORD lengthSize = sizeof(lengthText);
    if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        try { total = std::stoull(lengthText); } catch (...) { total = 0; }
    }
    // Read larger blocks and avoid an extra QueryDataAvailable round trip per
    // chunk. A megabyte is enough to keep WinHTTP busy without a large stack
    // allocation in the launcher.
    std::vector<char> buffer(1024 * 1024);
    ULONGLONG received = 0;
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(),
                             static_cast<DWORD>(buffer.size()), &read)) {
            throw std::runtime_error("The HTTPS download was interrupted.");
        }
        if (read == 0) break;
        received += read;
        consume(buffer.data(), read, total, received);
    }
}

std::string ReadHttpsText(std::string_view url) {
    std::string body;
    TransferHttps(CrackHttpsUrl(url), [&body](const char *data, size_t count, ULONGLONG, ULONGLONG) {
        if (count > 64 * 1024 - std::min<size_t>(body.size(), 64 * 1024)) {
            throw std::runtime_error("The update manifest is too large.");
        }
        body.append(data, count);
    });
    return body;
}

void DownloadHttpsFile(std::string_view url, const fs::path &destination) {
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Could not create the temporary client file.");
    g_splash.transferStarted = std::chrono::steady_clock::now();
    auto nextProgressDraw = g_splash.transferStarted;
    TransferHttps(CrackHttpsUrl(url), [&output, &nextProgressDraw](const char *data, size_t count,
                                                                   ULONGLONG total, ULONGLONG received) {
        output.write(data, static_cast<std::streamsize>(count));
        if (!output) throw std::runtime_error("Could not save the downloaded client.");
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - g_splash.transferStarted).count();
        const double bytesPerSecond = elapsed > 0.0 ? static_cast<double>(received) / elapsed : 0.0;
        if (now >= nextProgressDraw || (total > 0 && received >= total)) {
            const float progress = total > 0
                ? std::min(1.0f, static_cast<float>(received) / static_cast<float>(total))
                : -1.0f;
            DrawProgress(progress, "Downloading update...", total, received, bytesPerSecond);
            nextProgressDraw = now + std::chrono::milliseconds(100);
        }
    });
    output.close();
    if (!output) throw std::runtime_error("Could not close the downloaded client file.");
}

std::string Sha256File(const fs::path &path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        throw std::runtime_error("Could not initialize SHA-256.");
    }
    try {
        DWORD objectSize = 0;
        DWORD resultSize = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                              &resultSize, 0) < 0) throw std::runtime_error("SHA-256 operation failed.");
        std::vector<UCHAR> object(objectSize);
        if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) < 0) {
            throw std::runtime_error("Could not create the SHA-256 hash.");
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Could not open the client for verification.");
        std::array<char, 256 * 1024> buffer{};
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                                            static_cast<ULONG>(count), 0) < 0) {
                throw std::runtime_error("SHA-256 verification failed.");
            }
        }
        if (!input.eof()) throw std::runtime_error("Could not read the file during SHA-256 verification.");
        std::array<UCHAR, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
            throw std::runtime_error("Could not finalize SHA-256.");
        }
        std::string result;
        constexpr char hex[] = "0123456789abcdef";
        result.reserve(digest.size() * 2);
        for (UCHAR byte : digest) {
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 0x0f]);
        }
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return result;
    } catch (...) {
        if (hash != nullptr) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw;
    }
}
#endif

fs::path ExecutablePath() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) throw std::runtime_error("Could not locate the launcher.");
    buffer.resize(length);
    return fs::path(buffer);
#else
    std::error_code error;
    const fs::path path = fs::canonical("/proc/self/exe", error);
    if (error) throw std::runtime_error("Could not locate the launcher: " + error.message());
    return path;
#endif
}

fs::path InstallRoot() {
#ifdef _WIN32
    const wchar_t *localAppData = _wgetenv(L"LOCALAPPDATA");
    if (localAppData == nullptr || *localAppData == L'\0') throw std::runtime_error("LOCALAPPDATA is not defined.");
    return fs::path(localAppData) / L"Blubox";
#else
    const char *home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') throw std::runtime_error("HOME is not defined.");
    return fs::path(home) / ".local" / "share" / "Blubox";
#endif
}

std::string ReadManifestVersion(const fs::path &manifest) {
    std::ifstream input(manifest);
    if (!input) throw std::runtime_error("Manifest not found: " + manifest.string());
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::string key = "\"version\"";
    const size_t keyAt = contents.find(key);
    if (keyAt == std::string::npos) throw std::runtime_error("The manifest does not contain the version field.");
    const size_t colon = contents.find(':', keyAt + key.size());
    const size_t openQuote = contents.find('"', colon == std::string::npos ? keyAt : colon + 1);
    const size_t closeQuote = contents.find('"', openQuote == std::string::npos ? keyAt : openQuote + 1);
    if (colon == std::string::npos || openQuote == std::string::npos || closeQuote == std::string::npos) {
        throw std::runtime_error("The manifest version is invalid.");
    }
    const std::string version = contents.substr(openQuote + 1, closeQuote - openQuote - 1);
    if (version.empty() || !std::all_of(version.begin(), version.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '-' || c == '_';
        })) throw std::runtime_error("The version value contains invalid characters.");
    return version;
}

UpdateManifest ParseUpdateManifest(std::string_view contents) {
    nlohmann::json json;
    try { json = nlohmann::json::parse(contents); }
    catch (const std::exception &) { throw std::runtime_error("The update manifest JSON is invalid."); }
    if (!json.is_object() || !json.contains("version") || !json["version"].is_string()) {
        throw std::runtime_error("The update manifest does not contain a valid version.");
    }
    UpdateManifest manifest;
    manifest.version = json["version"].get<std::string>();
    if (manifest.version.empty() || !std::all_of(manifest.version.begin(), manifest.version.end(),
            [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-' || c == '_'; })) {
        throw std::runtime_error("The update manifest version is invalid.");
    }
    if (json.contains("downloadUrl") && json["downloadUrl"].is_string()) {
        manifest.downloadUrl = json["downloadUrl"].get<std::string>();
    }
    if (json.contains("sha256") && json["sha256"].is_string()) {
        manifest.sha256 = json["sha256"].get<std::string>();
    }
    const auto readString = [&json](const char *key, std::string &value) {
        if (json.contains(key) && json[key].is_string()) value = json[key].get<std::string>();
    };
    readString("studioVersion", manifest.studioVersion);
    readString("studioDownloadUrl", manifest.studioDownloadUrl);
    readString("studioSha256", manifest.studioSha256);
    readString("studioFontUrl", manifest.studioFontUrl);
    readString("studioFontSha256", manifest.studioFontSha256);
    readString("studioCodeFontUrl", manifest.studioCodeFontUrl);
    readString("studioCodeFontSha256", manifest.studioCodeFontSha256);
    readString("studioLauncherDownloadUrl", manifest.studioLauncherDownloadUrl);
    readString("studioLauncherSha256", manifest.studioLauncherSha256);
    readString("launcherVersion", manifest.launcherVersion);
    readString("launcherDownloadUrl", manifest.launcherDownloadUrl);
    readString("launcherSha256", manifest.launcherSha256);
    readString("studioLauncherVersion", manifest.studioLauncherVersion);
    if (!manifest.downloadUrl.empty() || !manifest.sha256.empty()) {
        if (manifest.downloadUrl.empty() || manifest.sha256.size() != 64 ||
            !std::all_of(manifest.sha256.begin(), manifest.sha256.end(), [](unsigned char c) {
                return std::isxdigit(c) != 0;
            })) throw std::runtime_error("The update manifest contains an invalid URL or SHA-256 hash.");
    }
    if (!manifest.studioVersion.empty()) {
        const auto validHash = [](const std::string &hash) {
            return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](unsigned char c) {
                return std::isxdigit(c) != 0;
            });
        };
        if (!manifest.launcherVersion.empty() &&
            (manifest.launcherDownloadUrl.empty() || !validHash(manifest.launcherSha256))) {
            throw std::runtime_error("The client launcher manifest is incomplete.");
        }
        if (manifest.studioDownloadUrl.empty() || !validHash(manifest.studioSha256) ||
            manifest.studioFontUrl.empty() || !validHash(manifest.studioFontSha256) ||
            manifest.studioCodeFontUrl.empty() || !validHash(manifest.studioCodeFontSha256) ||
            manifest.studioLauncherDownloadUrl.empty() || !validHash(manifest.studioLauncherSha256) ||
            manifest.studioLauncherVersion.empty()) {
            throw std::runtime_error("The manifest is incomplete: Studio, Studio Launcher, fonts, and SHA-256 checksums are required.");
        }
    }
    return manifest;
}

std::vector<unsigned long long> VersionParts(std::string_view version) {
    std::vector<unsigned long long> parts;
    size_t start = 0;
    while (start <= version.size()) {
        const size_t end = version.find('.', start);
        const std::string_view part = version.substr(start, end == std::string_view::npos ? end : end - start);
        size_t numericEnd = 0;
        while (numericEnd < part.size() && std::isdigit(static_cast<unsigned char>(part[numericEnd]))) ++numericEnd;
        unsigned long long number = 0;
        if (numericEnd > 0) {
            try { number = std::stoull(std::string(part.substr(0, numericEnd))); }
            catch (...) { number = 0; }
        }
        parts.push_back(number);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return parts;
}

bool IsNewer(std::string_view candidate, std::string_view installed) {
    const auto remoteParts = VersionParts(candidate);
    const auto localParts = VersionParts(installed);
    for (size_t i = 0; i < std::max(remoteParts.size(), localParts.size()); ++i) {
        const auto remote = i < remoteParts.size() ? remoteParts[i] : 0;
        const auto local = i < localParts.size() ? localParts[i] : 0;
        if (remote != local) return remote > local;
    }
    return candidate != installed;
}

std::string FormatBytes(double bytes) {
    constexpr const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    std::size_t unit = 0;
    while (bytes >= 1024.0 && unit + 1 < std::size(units)) {
        bytes /= 1024.0;
        ++unit;
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << bytes << ' ' << units[unit];
    return output.str();
}

std::string FormatRemainingTime(double seconds) {
    if (seconds <= 0.0) return "Almost done";
    if (seconds < 1.0) return "Less than a second remaining";
    const auto wholeSeconds = static_cast<unsigned int>(seconds);
    const unsigned int minutes = wholeSeconds / 60;
    const unsigned int remainingSeconds = wholeSeconds % 60;
    std::ostringstream output;
    output << "About ";
    if (minutes > 0) output << minutes << "m ";
    output << remainingSeconds << "s remaining";
    return output.str();
}

void InitializeSplashWindow() {
    g_splash = SplashState{};
    g_splash.font = LoadFontFromMemory(".ttf", blubox::kLauncherFontData,
                                       blubox::kLauncherFontDataSize, 48, nullptr, 0);
    g_splash.ownsFont = g_splash.font.texture.id != 0;
    if (!g_splash.ownsFont) g_splash.font = GetFontDefault();
    else SetTextureFilter(g_splash.font.texture, TEXTURE_FILTER_BILINEAR);

    DWORD lightPreference = 1;
#ifdef _WIN32
    DWORD preferenceBytes = sizeof(lightPreference);
    const LSTATUS themeRead = RegGetValueW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &lightPreference, &preferenceBytes);
    g_splash.darkMode = themeRead == ERROR_SUCCESS && lightPreference == 0;
#endif

    Image iconImage = LoadImageFromMemory(".png", blubox::kBrandIconData,
                                          blubox::kBrandIconDataSize);
    if (iconImage.data != nullptr) {
        ImageFormat(&iconImage, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        SetWindowIcon(iconImage);
        g_splash.icon = LoadTextureFromImage(iconImage);
        UnloadImage(iconImage);
        if (g_splash.icon.id != 0) SetTextureFilter(g_splash.icon, TEXTURE_FILTER_BILINEAR);
    }
    g_splash.transferStarted = std::chrono::steady_clock::now();
    SetExitKey(KEY_NULL);
}

void CloseSplashWindow() {
    if (g_splash.icon.id != 0) UnloadTexture(g_splash.icon);
    if (g_splash.ownsFont) UnloadFont(g_splash.font);
    if (IsWindowReady()) CloseWindow();
    g_splash = SplashState{};
}

void DrawCenteredSplashText(const char *text, float y, float fontSize, Color color) {
    const Vector2 size = MeasureTextEx(g_splash.font, text, fontSize, 0.5f);
    DrawTextEx(g_splash.font, text,
               Vector2{(static_cast<float>(GetScreenWidth()) - size.x) * 0.5f, y},
               fontSize, 0.5f, color);
}

void DrawProgress(float progress, std::string_view status, std::uint64_t totalBytes,
                  std::uint64_t transferredBytes, double bytesPerSecond) {
    const Color background = g_splash.darkMode ? Color{19, 19, 26, 255} : Color{248, 249, 250, 255};
    const Color border = g_splash.darkMode ? Color{46, 46, 62, 255} : Color{167, 227, 153, 255};
    const Color ink = g_splash.darkMode ? Color{226, 232, 240, 255} : Color{51, 51, 51, 255};
    const Color muted = g_splash.darkMode ? Color{148, 163, 184, 255} : Color{102, 102, 102, 255};
    const Color accent = g_splash.darkMode ? Color{0, 210, 255, 255} : Color{72, 179, 175, 255};
    const Color track = g_splash.darkMode ? Color{46, 46, 62, 255} : Color{224, 231, 234, 255};

    const float width = static_cast<float>(GetScreenWidth());
    const float height = static_cast<float>(GetScreenHeight());
    const Rectangle frame{width * 0.031f, height * 0.054f, width * 0.938f, height * 0.87f};
    const Rectangle closeButton{width - 71.0f, height * 0.068f, 38.0f, 34.0f};
    const Rectangle minimizeButton{width - 121.0f, height * 0.068f, 38.0f, 34.0f};
    const Rectangle cancelButton{(width - width * 0.20f) * 0.5f, height * 0.78f,
                                 width * 0.20f, height * 0.078f};

    BeginDrawing();
    ClearBackground(background);
    DrawRectangleRounded(frame, 0.012f, 12, background);
    DrawRectangleRoundedLines(frame, 0.012f, 12, border);

    DrawLineEx(Vector2{minimizeButton.x + 10.0f, minimizeButton.y + 17.0f},
               Vector2{minimizeButton.x + 28.0f, minimizeButton.y + 17.0f}, 2.0f, ink);
    DrawLineEx(Vector2{closeButton.x + 11.0f, closeButton.y + 8.0f},
               Vector2{closeButton.x + 27.0f, closeButton.y + 24.0f}, 2.0f, ink);
    DrawLineEx(Vector2{closeButton.x + 27.0f, closeButton.y + 8.0f},
               Vector2{closeButton.x + 11.0f, closeButton.y + 24.0f}, 2.0f, ink);

    const float iconSize = std::min(width * 0.235f, height * 0.38f);
    const float iconY = height * 0.15f;
    if (g_splash.icon.id != 0) {
        const Rectangle source{0.0f, 0.0f, static_cast<float>(g_splash.icon.width),
                               static_cast<float>(g_splash.icon.height)};
        const Rectangle destination{(width - iconSize) * 0.5f, iconY, iconSize, iconSize};
        DrawTexturePro(g_splash.icon, source, destination, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    } else {
        DrawCircle(static_cast<int>(width * 0.5f), static_cast<int>(iconY + iconSize * 0.5f),
                   iconSize * 0.43f, Color{225, 244, 235, 255});
        DrawCenteredSplashText("B", iconY + iconSize * 0.16f, iconSize * 0.68f,
                               Color{71, 111, 174, 255});
    }

    const std::string heading(status);
    DrawCenteredSplashText(heading.c_str(), iconY + iconSize + height * 0.036f,
                           height * 0.037f, ink);

    const float barWidth = width * 0.68f;
    const float barHeight = height * 0.026f;
    const float barX = (width - barWidth) * 0.5f;
    const float barY = height * 0.64f;
    const Rectangle trackRect{barX, barY, barWidth, barHeight};
    DrawRectangleRounded(trackRect, 0.5f, 12, track);
    if (progress >= 0.0f) {
        const float fillWidth = barWidth * std::clamp(progress, 0.0f, 1.0f);
        if (fillWidth > 0.0f) {
            DrawRectangleRounded(Rectangle{barX, barY, fillWidth, barHeight}, 0.5f, 12, accent);
        }
    } else {
        const float movingWidth = barWidth * 0.22f;
        const float phase = static_cast<float>(GetTime() * 0.65);
        const float travel = barWidth - movingWidth;
        const float sweep = (1.0f - std::abs(std::fmod(phase, 2.0f) - 1.0f)) * travel;
        DrawRectangleRounded(Rectangle{barX + sweep, barY, movingWidth, barHeight},
                             0.5f, 12, accent);
    }

    std::ostringstream details;
    if (totalBytes > 0) {
        const int percentage = static_cast<int>(std::clamp(progress, 0.0f, 1.0f) * 100.0f);
        details << FormatBytes(static_cast<double>(transferredBytes)) << " / "
                << FormatBytes(static_cast<double>(totalBytes)) << " (" << percentage << "%)";
    } else {
        details << FormatBytes(static_cast<double>(transferredBytes)) << " downloaded";
    }
    details << "  •  ";
    if (bytesPerSecond > 0.0) details << FormatBytes(bytesPerSecond) << "/s";
    else details << "Calculating speed...";
    details << "  •  ";
    if (totalBytes > transferredBytes && bytesPerSecond > 1.0) {
        details << FormatRemainingTime(static_cast<double>(totalBytes - transferredBytes) / bytesPerSecond);
    } else if (totalBytes == 0) {
        details << "Estimating time remaining...";
    } else {
        details << "Almost done";
    }
    const std::string detailText = details.str();
    const float detailSize = std::min(height * 0.026f, width / 54.0f);
    DrawCenteredSplashText(detailText.c_str(), barY + 31.0f, detailSize, muted);

    if (WindowShouldClose()) g_splash.cancelRequested = true;
    bool minimizeRequested = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        const Vector2 mouse = GetMousePosition();
        if (CheckCollisionPointRec(mouse, cancelButton) || CheckCollisionPointRec(mouse, closeButton)) {
            g_splash.cancelRequested = true;
        } else if (CheckCollisionPointRec(mouse, minimizeButton)) {
            minimizeRequested = true;
        } else if (mouse.y <= 50.0f) {
            g_splash.dragging = true;
        }
    }
    if (g_splash.dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        const Vector2 mouseDelta = GetMouseDelta();
        const Vector2 windowPosition = GetWindowPosition();
        SetWindowPosition(static_cast<int>(windowPosition.x + mouseDelta.x),
                          static_cast<int>(windowPosition.y + mouseDelta.y));
    } else if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        g_splash.dragging = false;
    }

    DrawRectangleRoundedLines(cancelButton, 0.18f, 12, accent);
    const char *cancelText = g_splash.cancelRequested ? "Cancelling..." : "Cancel";
    const float cancelFontSize = height * 0.029f;
    const Vector2 cancelSize = MeasureTextEx(g_splash.font, cancelText, cancelFontSize, 0.5f);
    DrawTextEx(g_splash.font, cancelText,
               Vector2{cancelButton.x + (cancelButton.width - cancelSize.x) * 0.5f,
                       cancelButton.y + (cancelButton.height - cancelSize.y) * 0.5f},
               cancelFontSize, 0.5f, accent);
    EndDrawing();
    if (minimizeRequested) MinimizeWindow();
    if (g_splash.cancelRequested) throw std::runtime_error("Update cancelled.");
}

void CopyWithProgress(const fs::path &source, const fs::path &destination) {
    std::ifstream input(source, std::ios::binary);
    if (!input) throw std::runtime_error("Client payload not found next to the launcher: " + source.string());
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Could not create the temporary client file.");
    std::error_code sizeError;
    const auto totalBytes = fs::file_size(source, sizeError);
    g_splash.transferStarted = std::chrono::steady_clock::now();
    auto nextProgressDraw = g_splash.transferStarted;
    std::array<char, 256 * 1024> buffer{};
    std::uintmax_t copied = 0;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            output.write(buffer.data(), count);
            if (!output) throw std::runtime_error("Could not copy the client.");
            copied += static_cast<std::uintmax_t>(count);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextProgressDraw || (!sizeError && copied >= totalBytes)) {
            const double elapsed = std::chrono::duration<double>(now - g_splash.transferStarted).count();
            const double rate = elapsed > 0.0 ? static_cast<double>(copied) / elapsed : 0.0;
            const float progress = !sizeError && totalBytes > 0
                ? std::min(1.0f, static_cast<float>(copied) / static_cast<float>(totalBytes))
                : -1.0f;
            DrawProgress(progress, "Installing Blubox...", sizeError ? 0 : totalBytes, copied, rate);
            nextProgressDraw = now + std::chrono::milliseconds(100);
        }
    }
    output.close();
    if (!input.eof()) throw std::runtime_error("Could not read the local client file.");
    DrawProgress(1.0f, "Starting Blubox...", sizeError ? 0 : totalBytes, copied, 0.0);
}

#ifdef _WIN32
std::wstring NarrowArgToWide(const char *text) {
    if (text == nullptr) return {};
    // Narrow argv on Windows uses the active code page; round-trip those bytes
    // back to UTF-16 before CreateProcessW rebuilds the argument vector.
    const int needed = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (needed <= 0) throw std::runtime_error("Could not convert a startup argument to Unicode.");
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), needed);
    result.pop_back();
    return result;
}

std::wstring QuoteWindowsArgument(const std::wstring &argument) {
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted += L'"';
        } else {
            quoted.append(slashes, L'\\');
            quoted += ch;
        }
        slashes = 0;
    }
    quoted.append(slashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}

void StartClient(const fs::path &client, int argc, char *argv[]) {
    std::wstring command = QuoteWindowsArgument(client.wstring());
    for (int i = 1; i < argc; ++i) command += L" " + QuoteWindowsArgument(NarrowArgToWide(argv[i]));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(client.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        client.parent_path().c_str(), &startup, &process)) {
        throw std::runtime_error("Could not start the client (error code " + std::to_string(GetLastError()) + ").");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}
#else
void StartClient(const fs::path &client, int argc, char *argv[]) {
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("Could not create the client process.");
    if (child == 0) {
        std::vector<char *> arguments;
        arguments.reserve(static_cast<size_t>(argc) + 1);
        arguments.push_back(const_cast<char *>(client.c_str()));
        for (int i = 1; i < argc; ++i) arguments.push_back(argv[i]);
        arguments.push_back(nullptr);
        execv(client.c_str(), arguments.data());
        _exit(127);
    }
}
#endif

fs::path EnsureStudioInstalled(const UpdateManifest &manifest, bool hasRemoteManifest,
                               const fs::path &installRoot, bool updateStudioLauncher) {
    const std::string launcherVersion = manifest.studioLauncherVersion.empty()
        ? manifest.studioVersion : manifest.studioLauncherVersion;
    const fs::path studioLauncherPath = installRoot / "launchers" / launcherVersion / "blubox_studio_launcher.exe";
    std::ifstream installedVersionFile(installRoot / "installed_studio_version.txt");
    std::string installedVersion;
    std::getline(installedVersionFile, installedVersion);
    const fs::path oldVersionDirectory = installRoot / "studio" / "versions" / installedVersion;
    const fs::path oldStudio = oldVersionDirectory / "blubox_studio.exe";

    if (!hasRemoteManifest) {
        if (!installedVersion.empty() && fs::exists(oldStudio) &&
            fs::exists(oldVersionDirectory / "assets/editor/fonts/InterVariable.ttf") &&
            fs::exists(oldVersionDirectory / "assets/editor/fonts/JetBrainsMono-Regular.ttf") &&
            (!updateStudioLauncher || fs::exists(studioLauncherPath))) {
#ifdef _WIN32
            if (updateStudioLauncher) {
                CreateBluboxStudioShortcuts(studioLauncherPath);
                RegisterBluboxStudioProtocolForCurrentUser(studioLauncherPath);
                RegisterBluboxStudioUninstaller(studioLauncherPath, installedVersion);
            }
#endif
            return oldStudio;
        }
        throw std::runtime_error("Could not install or repair Blubox Studio without a connection to the update feed.");
    }

    if (manifest.studioVersion.empty()) {
        throw std::runtime_error("The update feed does not include a Blubox Studio version.");
    }
    const fs::path versionDirectory = installRoot / "studio" / "versions" / manifest.studioVersion;
    const fs::path studioPath = versionDirectory / "blubox_studio.exe";
    const fs::path fontDirectory = versionDirectory / "assets" / "editor" / "fonts";
    const fs::path interfaceFont = fontDirectory / "InterVariable.ttf";
    const fs::path codeFont = fontDirectory / "JetBrainsMono-Regular.ttf";
    const auto fileMatches = [](const fs::path &path, const std::string &expectedHash) {
        if (!fs::exists(path)) return false;
#ifdef _WIN32
        try { return Sha256File(path) == expectedHash; }
        catch (...) { return false; }
#else
        (void)expectedHash;
        return false;
#endif
    };

    const bool needStudio = !fileMatches(studioPath, manifest.studioSha256);
    const bool needInterfaceFont = !fileMatches(interfaceFont, manifest.studioFontSha256);
    const bool needCodeFont = !fileMatches(codeFont, manifest.studioCodeFontSha256);
    bool studioLauncherIsRunningFromTarget = false;
#ifdef _WIN32
    if (updateStudioLauncher) {
        studioLauncherIsRunningFromTarget =
            ExecutablePath().lexically_normal() == studioLauncherPath.lexically_normal();
    }
#endif
    const bool needStudioLauncher = updateStudioLauncher && !studioLauncherIsRunningFromTarget &&
        !fileMatches(studioLauncherPath, manifest.studioLauncherSha256);
    const bool needInstall = needStudio || needInterfaceFont || needCodeFont || needStudioLauncher;

    if (needInstall) {
#ifdef _WIN32
        SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
        InitWindow(960, 594, "Blubox Installer");
        if (!IsWindowReady()) throw std::runtime_error("Could not open the Blubox installer window.");
        SetTargetFPS(60);
        InitializeSplashWindow();
        fs::create_directories(versionDirectory);
        fs::create_directories(fontDirectory);
        if (needStudioLauncher) fs::create_directories(studioLauncherPath.parent_path());
        const auto downloadVerified = [](const std::string &url, const std::string &hash,
                                         const fs::path &destination, std::string_view title) {
            const fs::path staging = fs::path(destination.wstring() + L".tmp");
            fs::remove(staging);
            DownloadHttpsFile(url, staging);
            if (Sha256File(staging) != hash) {
                fs::remove(staging);
                throw std::runtime_error("SHA-256 verification failed for " + std::string(title) + ".");
            }
            fs::remove(destination);
            fs::rename(staging, destination);
        };
        try {
            if (needStudio) downloadVerified(manifest.studioDownloadUrl, manifest.studioSha256,
                                              studioPath, "Blubox Studio");
            if (needInterfaceFont) downloadVerified(manifest.studioFontUrl, manifest.studioFontSha256,
                                                     interfaceFont, "font Inter");
            if (needCodeFont) downloadVerified(manifest.studioCodeFontUrl, manifest.studioCodeFontSha256,
                                                codeFont, "font JetBrains Mono");
            if (needStudioLauncher) downloadVerified(manifest.studioLauncherDownloadUrl,
                                                      manifest.studioLauncherSha256,
                                                      studioLauncherPath, "launcher Studio");
            CloseSplashWindow();
        } catch (...) {
            CloseSplashWindow();
            throw;
        }
#else
        throw std::runtime_error("Automatic installation of Blubox Studio is available only on Windows.");
#endif
    }

    fs::create_directories(installRoot);
    std::ofstream versionFile(installRoot / "installed_studio_version.txt", std::ios::trunc);
    versionFile << manifest.studioVersion << '\n';
    if (!versionFile) throw std::runtime_error("Could not save the installed Studio version.");

#ifdef _WIN32
    CreateBluboxStudioShortcuts(studioLauncherPath);
    RegisterBluboxStudioProtocolForCurrentUser(studioLauncherPath);
    RegisterBluboxStudioUninstaller(studioLauncherPath, manifest.studioVersion);
#endif
    return studioPath;
}

fs::path EnsureClientLauncherInstalled(const UpdateManifest &manifest, bool hasRemoteManifest,
                                       const fs::path &installRoot) {
    if (!hasRemoteManifest || manifest.launcherVersion.empty()) {
        return ExecutablePath();
    }
    const fs::path launcherPath = installRoot / "launchers" / manifest.launcherVersion / "blubox_launcher.exe";
    bool matches = false;
#ifdef _WIN32
    if (fs::exists(launcherPath)) {
        try { matches = Sha256File(launcherPath) == manifest.launcherSha256; }
        catch (...) { matches = false; }
    }
    if (!matches) {
        fs::create_directories(launcherPath.parent_path());
        const fs::path currentLauncher = ExecutablePath();
        const fs::path staging = fs::path(launcherPath.wstring() + L".tmp");
        fs::remove(staging);
        SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
        InitWindow(960, 594, "Blubox Launcher Repair");
        if (!IsWindowReady()) throw std::runtime_error("Could not open the launcher repair window.");
        SetTargetFPS(60);
        InitializeSplashWindow();
        try {
            if (Sha256File(currentLauncher) == manifest.launcherSha256) {
                fs::copy_file(currentLauncher, staging, fs::copy_options::overwrite_existing);
            } else {
                DownloadHttpsFile(manifest.launcherDownloadUrl, staging);
            }
            if (Sha256File(staging) != manifest.launcherSha256) {
                fs::remove(staging);
                throw std::runtime_error("Client launcher SHA-256 verification failed.");
            }
            fs::remove(launcherPath);
            fs::rename(staging, launcherPath);
            CloseSplashWindow();
        } catch (...) {
            CloseSplashWindow();
            throw;
        }
    }
#else
    (void)matches;
#endif
    return launcherPath;
}

} // namespace

int RunLauncher(const LaunchOptions &options, int argc, char *argv[]) {
    const fs::path launcherDirectory = ExecutablePath().parent_path();
    const fs::path installRoot = InstallRoot();
    std::ifstream installedVersionFile(installRoot / (options.launchStudio ? "installed_studio_version.txt" : "installed_version.txt"));
    std::string installedVersion;
    std::getline(installedVersionFile, installedVersion);
    const fs::path previouslyInstalledClient = options.launchStudio
        ? installRoot / "studio" / "versions" / installedVersion / "blubox_studio.exe"
        : installRoot / "versions" / installedVersion / "blubox_client.exe";
    const bool hasPreviousInstall = !installedVersion.empty() && fs::exists(previouslyInstalledClient);

    UpdateManifest manifest;
    bool hasRemoteManifest = false;
#ifdef _WIN32
    try {
        const char *configuredUrl = std::getenv("BLUBOX_MANIFEST_URL");
        const std::string manifestUrl = AddManifestCacheBuster(
            configuredUrl != nullptr && *configuredUrl != '\0' ? configuredUrl : kManifestUrl);
        manifest = ParseUpdateManifest(ReadHttpsText(manifestUrl));
        hasRemoteManifest = true;
    } catch (const std::exception &) {
        // Existing installs remain launchable offline. The sidecar manifest
        // and executable can also advance a local development install.
        if (hasPreviousInstall) {
            if (options.launchStudio) {
                StartClient(previouslyInstalledClient, argc, argv);
                return 0;
            }
            try {
                manifest.version = ReadManifestVersion(launcherDirectory / "version.json");
                if (!IsNewer(manifest.version, installedVersion)) {
                    StartClient(previouslyInstalledClient, argc, argv);
                    return 0;
                }
            } catch (const std::exception &) {
                StartClient(previouslyInstalledClient, argc, argv);
                return 0;
            }
        } else {
            manifest.version = ReadManifestVersion(launcherDirectory / "version.json");
        }
    }
#else
    manifest.version = ReadManifestVersion(launcherDirectory / "version.json");
#endif

    if (options.launchStudio) {
        const fs::path studio = EnsureStudioInstalled(manifest, hasRemoteManifest, installRoot, true);
        StartClient(studio, argc, argv);
        return 0;
    }

    // The game launcher bootstraps and repairs Studio before starting the game.
    // The dedicated Studio launcher uses the same feed but skips this bootstrap.
    (void)EnsureStudioInstalled(manifest, hasRemoteManifest, installRoot, true);
    const fs::path installedLauncher = EnsureClientLauncherInstalled(manifest, hasRemoteManifest, installRoot);

    const std::string targetVersion = manifest.version;
    const fs::path versionDirectory = installRoot / "versions" / targetVersion;
    fs::path installedClient = versionDirectory / "blubox_client.exe";
    const bool remoteIsNewer = hasPreviousInstall && IsNewer(targetVersion, installedVersion);
    if (hasPreviousInstall && !remoteIsNewer) installedClient = previouslyInstalledClient;
    const bool needsUpdate = !fs::exists(installedClient) || (hasRemoteManifest && remoteIsNewer);

    if (needsUpdate) {
        SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
        InitWindow(960, 594, "Blubox Installer");
        if (!IsWindowReady()) throw std::runtime_error("Could not open the update window.");
        SetTargetFPS(60);
        InitializeSplashWindow();
        fs::create_directories(versionDirectory);
        const fs::path stagingClient = versionDirectory / "blubox_client.exe.tmp";
        try {
            fs::remove(stagingClient);
            if (hasRemoteManifest && !manifest.downloadUrl.empty()) {
#ifdef _WIN32
                DownloadHttpsFile(manifest.downloadUrl, stagingClient);
                if (Sha256File(stagingClient) != manifest.sha256) {
                    throw std::runtime_error("The downloaded client SHA-256 checksum does not match.");
                }
#else
                throw std::runtime_error("Automatic downloads are available only on Windows.");
#endif
            } else {
                CopyWithProgress(launcherDirectory / "blubox_client.exe", stagingClient);
            }
            fs::remove(installedClient);
            fs::rename(stagingClient, installedClient);
            DrawProgress(1.0f, "Starting Blubox...", 0, 0, 0.0);
            CloseSplashWindow();
        } catch (...) {
            CloseSplashWindow();
            throw;
        }
    }

    if (installedClient.parent_path() == versionDirectory) {
        fs::create_directories(installRoot);
        std::ofstream versionFile(installRoot / "installed_version.txt", std::ios::trunc);
        versionFile << targetVersion << '\n';
        if (!versionFile) throw std::runtime_error("Could not save the installed version.");
    }

#ifdef _WIN32
    RegisterBluboxProtocolForCurrentUser(installedLauncher);
    CreateBluboxClientShortcuts(installedLauncher);
    RegisterBluboxClientUninstaller(installedLauncher, targetVersion);
#endif
    StartClient(installedClient, argc, argv);
    return 0;
}
