#include "Launcher.hpp"
#include "BluboxLauncherFont.hpp"
#include "BluboxBrandIcon.hpp"
#include "WindowsProtocolRegistration.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <fcntl.h>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include "HttpClient.hpp"

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
#include <bcrypt.h>
#else
#include <sys/types.h>
#include <unistd.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#endif
#endif

namespace fs = std::filesystem;

namespace {

constexpr char kManifestUrl[] =
    "https://raw.githubusercontent.com/vampeye123421/Blubox/main/version.json";
#ifdef _WIN32
const fs::path kClientBinaryName = L"blubox_client.exe";
const fs::path kStudioBinaryName = L"blubox_studio.exe";
const fs::path kClientLauncherName = L"blubox_launcher.exe";
const fs::path kStudioLauncherName = L"blubox_studio_launcher.exe";
#else
const fs::path kClientBinaryName = "blubox_client";
const fs::path kStudioBinaryName = "blubox_studio";
const fs::path kClientLauncherName = "blubox_launcher";
const fs::path kStudioLauncherName = "blubox_studio_launcher";
#endif

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
template <typename Function>
Function LoadSystemFunction(HMODULE module, const char *name) {
    const FARPROC address = module != nullptr ? GetProcAddress(module, name) : nullptr;
    Function function{};
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

void ApplySplashWindowCorners() {
    const HWND window = static_cast<HWND>(GetWindowHandle());
    if (window == nullptr) return;

    // Ask Windows 11 to round the actual borderless window, not just the
    // inset panel drawn by Raylib. Older Windows versions get a region fallback.
    constexpr DWORD kDwmWindowCornerPreference = 33;
    constexpr DWORD kDwmWindowCornerRound = 2;
    const DWORD preference = kDwmWindowCornerRound;
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm != nullptr) {
        using SetWindowAttributeFn = HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
        const auto setWindowAttribute = LoadSystemFunction<SetWindowAttributeFn>(
            dwm, "DwmSetWindowAttribute");
        const bool roundedByDwm = setWindowAttribute != nullptr &&
            SUCCEEDED(setWindowAttribute(window, kDwmWindowCornerPreference,
                                         &preference, sizeof(preference)));
        FreeLibrary(dwm);
        if (roundedByDwm) return;
    }

    HMODULE gdi = LoadLibraryW(L"gdi32.dll");
    if (gdi == nullptr) return;
    using CreateRoundRectRegionFn = HRGN (WINAPI *)(int, int, int, int, int, int);
    using SetWindowRegionFn = int (WINAPI *)(HWND, HRGN, BOOL);
    using DeleteGdiObjectFn = BOOL (WINAPI *)(void *);
    using GetWindowRectFn = BOOL (WINAPI *)(HWND, LPRECT);
    const auto createRoundRectRegion = LoadSystemFunction<CreateRoundRectRegionFn>(
        gdi, "CreateRoundRectRgn");
    const auto deleteGdiObject = LoadSystemFunction<DeleteGdiObjectFn>(gdi, "DeleteObject");
    const auto setWindowRegion = LoadSystemFunction<SetWindowRegionFn>(
        GetModuleHandleW(L"user32.dll"), "SetWindowRgn");
    const auto getWindowRect = LoadSystemFunction<GetWindowRectFn>(
        GetModuleHandleW(L"user32.dll"), "GetWindowRect");
    RECT bounds{};
    if (createRoundRectRegion != nullptr && setWindowRegion != nullptr &&
        getWindowRect != nullptr && getWindowRect(window, &bounds)) {
        HRGN region = createRoundRectRegion(0, 0, bounds.right - bounds.left,
                                            bounds.bottom - bounds.top, 36, 36);
        if (region != nullptr && setWindowRegion(window, region, TRUE) == 0 &&
            deleteGdiObject != nullptr) {
            deleteGdiObject(region);
        }
    }
    FreeLibrary(gdi);
}
#endif

std::string AddManifestCacheBuster(std::string url) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    url += url.find('?') == std::string::npos ? "?" : "&";
    url += "blubox_check=" + std::to_string(now);
    return url;
}

std::string CrackHttpsUrl(std::string_view url) {
    if (!url.starts_with("https://") || url.size() > 8192) {
        throw std::runtime_error("The manifest must use a valid HTTPS URL.");
    }
    return std::string(url);
}

template <typename Consumer>
void TransferHttps(const std::string &url, Consumer &&consume) {
    blubox::platform::HttpRequest request;
    request.url = url;
    request.timeoutMs = 120000;
    request.maxResponseBytes = std::numeric_limits<std::size_t>::max();
    std::uint64_t total = 0;
    std::uint64_t received = 0;
    request.onChunk = [&consume, &total, &received](const char *data, std::size_t count) {
        received += count;
        consume(data, count, total, received);
    };
    request.onProgress = [&consume, &total, &received](std::uint64_t current, std::uint64_t expected) {
        received = std::max(received, current);
        total = expected;
        consume(nullptr, 0, total, received);
    };
    const auto response = blubox::platform::PerformHttpRequest(request);
    if (response.status != 200) {
        throw std::runtime_error("The update server returned HTTP " +
                                 std::to_string(response.status) + ".");
    }
}

std::string ReadHttpsText(std::string_view url) {
    std::string body;
    TransferHttps(CrackHttpsUrl(url), [&body](const char *data, size_t count, std::uint64_t, std::uint64_t) {
        if (count == 0) return;
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
                                                                   std::uint64_t total, std::uint64_t received) {
        if (count > 0) {
            output.write(data, static_cast<std::streamsize>(count));
            if (!output) throw std::runtime_error("Could not save the downloaded client.");
        }
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
#ifdef _WIN32
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
#else
    CC_SHA256_CTX context;
    if (CC_SHA256_Init(&context) == 0) throw std::runtime_error("Could not initialize SHA-256.");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open the client for verification.");
    std::array<char, 256 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && CC_SHA256_Update(&context, buffer.data(), static_cast<CC_LONG>(count)) == 0) {
            throw std::runtime_error("SHA-256 verification failed.");
        }
    }
    if (!input.eof()) throw std::runtime_error("Could not read the file during SHA-256 verification.");
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
    if (CC_SHA256_Final(digest.data(), &context) == 0) throw std::runtime_error("Could not finalize SHA-256.");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (unsigned char byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
#endif
}

fs::path ExecutablePath() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) throw std::runtime_error("Could not locate the launcher.");
    buffer.resize(length);
    return fs::path(buffer);
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        throw std::runtime_error("Could not locate the launcher.");
    }
    std::error_code error;
    const fs::path path = fs::canonical(buffer.c_str(), error);
    if (error) throw std::runtime_error("Could not locate the launcher: " + error.message());
    return path;
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
#elif defined(__APPLE__)
    const char *home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') throw std::runtime_error("HOME is not defined.");
    return fs::path(home) / "Library" / "Application Support" / "Blubox";
#else
    const char *home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') throw std::runtime_error("HOME is not defined.");
    return fs::path(home) / ".local" / "share" / "Blubox";
#endif
}

void PruneVersionDirectories(const fs::path &root,
                             const std::vector<fs::path> &preservedDirectories) {
    std::error_code error;
    if (!fs::is_directory(root, error)) return;
    const fs::path normalizedRoot = fs::absolute(root, error).lexically_normal();
    if (error) return;
    for (fs::directory_iterator it(normalizedRoot, error), end; !error && it != end; it.increment(error)) {
        std::error_code typeError;
        if (!it->is_directory(typeError) || typeError) continue;
        const fs::path candidate = it->path().lexically_normal();
        bool preserve = false;
        for (const fs::path &directory : preservedDirectories) {
            if (!directory.empty() && candidate == fs::absolute(directory, typeError).lexically_normal() && !typeError) {
                preserve = true;
                break;
            }
            typeError.clear();
        }
        if (preserve) continue;
        std::error_code removeError;
        fs::remove_all(candidate, removeError);
        // A previous app version may still be running. Leave locked files in
        // place and retry pruning the folder the next time the launcher runs.
    }
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
    const nlohmann::json *platformPayload = &json;
#ifdef __APPLE__
    if (!json.contains("platforms") || !json["platforms"].is_object() ||
        !json["platforms"].contains("macos") || !json["platforms"]["macos"].is_object()) {
        throw std::runtime_error("The update manifest does not contain macOS download metadata.");
    }
    platformPayload = &json["platforms"]["macos"];
#endif
    const auto readString = [&json, platformPayload](const char *key, std::string &value) {
        if (platformPayload->contains(key) && (*platformPayload)[key].is_string()) {
            value = (*platformPayload)[key].get<std::string>();
        }
#ifndef __APPLE__
        else if (json.contains(key) && json[key].is_string()) {
            value = json[key].get<std::string>();
        }
#else
        else if (std::string_view(key) != "downloadUrl" &&
                 std::string_view(key) != "sha256" &&
                 std::string_view(key) != "studioDownloadUrl" &&
                 std::string_view(key) != "studioSha256" &&
                 std::string_view(key) != "studioLauncherDownloadUrl" &&
                 std::string_view(key) != "studioLauncherSha256" &&
                 std::string_view(key) != "launcherDownloadUrl" &&
                 std::string_view(key) != "launcherSha256" &&
                 json.contains(key) && json[key].is_string()) {
            value = json[key].get<std::string>();
        }
#endif
    };
    readString("downloadUrl", manifest.downloadUrl);
    readString("sha256", manifest.sha256);
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
    if (manifest.downloadUrl.empty() || manifest.sha256.size() != 64 ||
        !std::all_of(manifest.sha256.begin(), manifest.sha256.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        })) throw std::runtime_error("The update manifest is missing a valid client URL or SHA-256 hash.");
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

fs::path FindNewestVersionedLauncher(const fs::path &launchersRoot, const fs::path &executableName) {
    std::error_code error;
    fs::path newest;
    std::string newestVersion;
    if (!fs::is_directory(launchersRoot, error)) return newest;
    for (fs::directory_iterator it(launchersRoot, error), end; !error && it != end; it.increment(error)) {
        std::error_code candidateError;
        if (!it->is_directory(candidateError) || candidateError) continue;
        const fs::path candidate = it->path() / executableName;
        if (!fs::is_regular_file(candidate, candidateError) || candidateError) continue;
        const std::string candidateVersion = it->path().filename().string();
        if (newest.empty() || IsNewer(candidateVersion, newestVersion)) {
            newest = candidate;
            newestVersion = candidateVersion;
        }
    }
    return newest;
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

#ifdef _WIN32
    ApplySplashWindowCorners();
#endif

#ifdef _WIN32
    DWORD lightPreference = 1;
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
    details << "  |  ";
    if (bytesPerSecond > 0.0) details << FormatBytes(bytesPerSecond) << "/s";
    else details << "Calculating speed...";
    details << "  |  ";
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
    std::vector<char *> arguments;
    arguments.reserve(static_cast<size_t>(argc) + 1);
    arguments.push_back(const_cast<char *>(client.c_str()));
    for (int i = 1; i < argc; ++i) arguments.push_back(argv[i]);
    arguments.push_back(nullptr);

    int launchErrorPipe[2]{};
    if (pipe(launchErrorPipe) != 0) {
        throw std::runtime_error("Could not prepare the client process.");
    }
    if (fcntl(launchErrorPipe[1], F_SETFD, FD_CLOEXEC) == -1) {
        const int error = errno;
        close(launchErrorPipe[0]);
        close(launchErrorPipe[1]);
        throw std::runtime_error("Could not prepare the client process: " +
                                 std::string(std::strerror(error)));
    }

    const pid_t child = fork();
    if (child < 0) {
        const int error = errno;
        close(launchErrorPipe[0]);
        close(launchErrorPipe[1]);
        throw std::runtime_error("Could not create the client process: " +
                                 std::string(std::strerror(error)));
    }
    if (child == 0) {
        close(launchErrorPipe[0]);
        if (chdir(client.parent_path().c_str()) != 0) {
            const int error = errno;
            (void)write(launchErrorPipe[1], &error, sizeof(error));
            _exit(127);
        }
        execv(client.c_str(), arguments.data());
        const int error = errno;
        (void)write(launchErrorPipe[1], &error, sizeof(error));
        _exit(127);
    }
    close(launchErrorPipe[1]);
    int launchError = 0;
    ssize_t bytesRead = 0;
    do {
        bytesRead = read(launchErrorPipe[0], &launchError, sizeof(launchError));
    } while (bytesRead < 0 && errno == EINTR);
    const int readError = errno;
    close(launchErrorPipe[0]);
    if (bytesRead < 0) {
        throw std::runtime_error("Could not confirm the client process started: " +
                                 std::string(std::strerror(readError)));
    }
    if (bytesRead > 0) {
        throw std::runtime_error("Could not start the client: " +
                                 std::string(std::strerror(launchError)));
    }
}
#endif

fs::path EnsureStudioInstalled(const UpdateManifest &manifest, bool hasRemoteManifest,
                               const fs::path &installRoot, bool updateStudioLauncher) {
    const std::string launcherVersion = manifest.studioLauncherVersion.empty()
        ? manifest.studioVersion : manifest.studioLauncherVersion;
    const fs::path studioLauncherPath = launcherVersion.empty()
        ? FindNewestVersionedLauncher(installRoot / "launchers", kStudioLauncherName)
        : installRoot / "launchers" / launcherVersion / kStudioLauncherName;
    std::ifstream installedVersionFile(installRoot / "installed_studio_version.txt");
    std::string installedVersion;
    std::getline(installedVersionFile, installedVersion);
    const fs::path oldVersionDirectory = installRoot / "studio" / "versions" / installedVersion;
    const fs::path oldStudio = oldVersionDirectory / kStudioBinaryName;

    if (!hasRemoteManifest) {
        if (!installedVersion.empty() && fs::exists(oldStudio) &&
            fs::exists(oldVersionDirectory / "assets/editor/fonts/InterVariable.ttf") &&
            fs::exists(oldVersionDirectory / "assets/editor/fonts/JetBrainsMono-Regular.ttf") &&
            (!updateStudioLauncher || (!studioLauncherPath.empty() && fs::exists(studioLauncherPath)))) {
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
    const fs::path studioPath = versionDirectory / kStudioBinaryName;
    const fs::path fontDirectory = versionDirectory / "assets" / "editor" / "fonts";
    const fs::path interfaceFont = fontDirectory / "InterVariable.ttf";
    const fs::path codeFont = fontDirectory / "JetBrainsMono-Regular.ttf";
    const auto fileMatches = [](const fs::path &path, const std::string &expectedHash) {
        if (!fs::exists(path)) return false;
        try { return Sha256File(path) == expectedHash; }
        catch (...) { return false; }
    };

    const bool needStudio = !fileMatches(studioPath, manifest.studioSha256);
    const bool needInterfaceFont = !fileMatches(interfaceFont, manifest.studioFontSha256);
    const bool needCodeFont = !fileMatches(codeFont, manifest.studioCodeFontSha256);
    bool studioLauncherIsRunningFromTarget = false;
#if defined(_WIN32) || defined(__APPLE__)
    if (updateStudioLauncher) {
        studioLauncherIsRunningFromTarget =
            ExecutablePath().lexically_normal() == studioLauncherPath.lexically_normal();
    }
#endif
    const bool needStudioLauncher = updateStudioLauncher && !studioLauncherIsRunningFromTarget &&
        !fileMatches(studioLauncherPath, manifest.studioLauncherSha256);
    const bool needInstall = needStudio || needInterfaceFont || needCodeFont || needStudioLauncher;

    LogLauncherDiagnostic("Studio bootstrap: target=" + manifest.studioVersion +
        ", installed=" + installedVersion + ", update=" + (needInstall ? "yes" : "no"));

    if (needInstall) {
#if defined(_WIN32) || defined(__APPLE__)
        SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_HIGHDPI);
        InitWindow(960, 594, "Blubox Installer");
        if (!IsWindowReady()) throw std::runtime_error("Could not open the Blubox installer window.");
        SetTargetFPS(60);
        InitializeSplashWindow();
        fs::create_directories(versionDirectory);
        fs::create_directories(fontDirectory);
        if (needStudioLauncher) fs::create_directories(studioLauncherPath.parent_path());
#ifdef __APPLE__
        const auto downloadVerified = [&studioPath, &studioLauncherPath](
#else
        const auto downloadVerified = [](
#endif
                                         const std::string &url, const std::string &hash,
                                         const fs::path &destination, std::string_view title) {
            const fs::path staging = fs::path(destination.string() + ".tmp");
            fs::remove(staging);
            DownloadHttpsFile(url, staging);
            if (Sha256File(staging) != hash) {
                fs::remove(staging);
                throw std::runtime_error("SHA-256 verification failed for " + std::string(title) + ".");
            }
            fs::remove(destination);
            fs::rename(staging, destination);
#ifdef __APPLE__
            if (destination == studioPath || destination == studioLauncherPath) {
                fs::permissions(destination,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                    fs::perms::group_read | fs::perms::group_exec |
                    fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace);
            }
#endif
        };
        try {
            LogLauncherDiagnostic("Studio bootstrap: downloading or repairing Studio files.");
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
            LogLauncherDiagnostic("Studio bootstrap: repair completed.");
        } catch (const std::exception &error) {
            CloseSplashWindow();
            for (const fs::path &destination : {studioPath, interfaceFont, codeFont, studioLauncherPath}) {
                std::error_code removeError;
                fs::remove(fs::path(destination.string() + ".tmp"), removeError);
            }
            LogLauncherDiagnostic(std::string("Studio update failed: ") + error.what());

            const bool previousStudioIsUsable = !installedVersion.empty() && fs::exists(oldStudio) &&
                fs::exists(oldVersionDirectory / "assets/editor/fonts/InterVariable.ttf") &&
                fs::exists(oldVersionDirectory / "assets/editor/fonts/JetBrainsMono-Regular.ttf");
            if (!previousStudioIsUsable) {
                throw std::runtime_error(std::string("Blubox Studio could not be updated and no working installed copy is available. ") +
                    error.what() + " Download the current Blubox setup from the official website and run it to repair Studio.");
            }

            LogLauncherDiagnostic("Continuing with the last working Studio version " + installedVersion + ".");
#ifdef _WIN32
            if (updateStudioLauncher) {
                const fs::path fallbackStudioLauncher = FindNewestVersionedLauncher(
                    installRoot / "launchers", kStudioLauncherName);
                if (!fallbackStudioLauncher.empty()) {
                    RegisterBluboxStudioProtocolForCurrentUser(fallbackStudioLauncher);
                    CreateBluboxStudioShortcuts(fallbackStudioLauncher);
                    RegisterBluboxStudioUninstaller(fallbackStudioLauncher, installedVersion);
                }
            }
#endif
            return oldStudio;
        } catch (...) {
            CloseSplashWindow();
            for (const fs::path &destination : {studioPath, interfaceFont, codeFont, studioLauncherPath}) {
                std::error_code removeError;
                fs::remove(fs::path(destination.string() + ".tmp"), removeError);
            }
            throw;
        }
#endif
    }

    fs::create_directories(installRoot);
    std::ofstream versionFile(installRoot / "installed_studio_version.txt", std::ios::trunc);
    versionFile << manifest.studioVersion << '\n';
    if (!versionFile) throw std::runtime_error("Could not save the installed Studio version.");
    PruneVersionDirectories(installRoot / "studio" / "versions", {versionDirectory});

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
    const fs::path launcherPath = installRoot / "launchers" / manifest.launcherVersion / kClientLauncherName;
    bool matches = false;
#if defined(_WIN32) || defined(__APPLE__)
    if (fs::exists(launcherPath)) {
        try { matches = Sha256File(launcherPath) == manifest.launcherSha256; }
        catch (...) { matches = false; }
    }
    if (!matches) {
        fs::create_directories(launcherPath.parent_path());
        const fs::path currentLauncher = ExecutablePath();
        const fs::path staging = fs::path(launcherPath.string() + ".tmp");
        fs::remove(staging);
        SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_HIGHDPI);
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
#ifdef __APPLE__
            fs::permissions(launcherPath,
                fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                fs::perms::group_read | fs::perms::group_exec |
                fs::perms::others_read | fs::perms::others_exec,
                fs::perm_options::replace);
#endif
            CloseSplashWindow();
        } catch (...) {
            CloseSplashWindow();
            throw;
        }
    }
#endif
    return launcherPath;
}

} // namespace

void LogLauncherDiagnostic(std::string_view message) noexcept {
    const char *enabled = std::getenv("BLUBOX_UPDATER_DEBUG");
    if (enabled == nullptr || std::string_view(enabled) != "1") return;
    try {
#ifdef _WIN32
        const wchar_t *localAppData = _wgetenv(L"LOCALAPPDATA");
        if (localAppData != nullptr && *localAppData != L'\0') {
            const fs::path logDirectory = fs::path(localAppData) / L"Blubox" / L"logs";
            fs::create_directories(logDirectory);
            std::ofstream output(logDirectory / L"launcher-debug.log", std::ios::binary | std::ios::app);
            if (output) {
                for (const char ch : message) output.put(ch == '\r' || ch == '\n' ? ' ' : ch);
                output.put('\n');
            }
        }
#endif
        std::cout << "[updater] " << message << '\n';
    } catch (...) {
        // Debug logging must not affect update or launch behavior.
    }
}

int RunLauncher(const LaunchOptions &options, int argc, char *argv[]) {
    LogLauncherDiagnostic(std::string("Starting ") + (options.launchStudio ? "Studio" : "client") + " update flow.");
    const fs::path launcherDirectory = ExecutablePath().parent_path();
    const fs::path installRoot = InstallRoot();
#ifdef _WIN32
    // Repair the protocol association on every launch, including offline and
    // fallback paths. A verified manifest update below will move it to the
    // newly installed versioned launcher before the app is started.
    if (options.launchStudio) {
        RegisterBluboxStudioProtocolForCurrentUser(ExecutablePath());
    } else {
        RegisterBluboxProtocolForCurrentUser(ExecutablePath());
    }
#endif
    std::ifstream installedVersionFile(installRoot / (options.launchStudio ? "installed_studio_version.txt" : "installed_version.txt"));
    std::string installedVersion;
    std::getline(installedVersionFile, installedVersion);
    const fs::path previouslyInstalledClient = options.launchStudio
        ? installRoot / "studio" / "versions" / installedVersion / kStudioBinaryName
        : installRoot / "versions" / installedVersion / kClientBinaryName;
    const bool hasPreviousInstall = !installedVersion.empty() && fs::exists(previouslyInstalledClient);

    UpdateManifest manifest;
    bool hasRemoteManifest = false;
#if defined(_WIN32) || defined(__APPLE__)
    try {
        LogLauncherDiagnostic("Reading the update manifest over HTTPS.");
        const char *configuredUrl = std::getenv("BLUBOX_MANIFEST_URL");
        const std::string manifestUrl = AddManifestCacheBuster(
            configuredUrl != nullptr && *configuredUrl != '\0' ? configuredUrl : kManifestUrl);
        manifest = ParseUpdateManifest(ReadHttpsText(manifestUrl));
        hasRemoteManifest = true;
        LogLauncherDiagnostic("Manifest received: client=" + manifest.version + ", Studio=" + manifest.studioVersion +
            ", launcher=" + manifest.launcherVersion + ".");
    } catch (const std::exception &error) {
        LogLauncherDiagnostic(std::string("Manifest request failed: ") + error.what());
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
        LogLauncherDiagnostic("Preparing the Studio executable.");
        const fs::path studio = EnsureStudioInstalled(manifest, hasRemoteManifest, installRoot, true);
        fs::path installedStudioLauncher = FindNewestVersionedLauncher(
            installRoot / "launchers", kStudioLauncherName);
        if (installedStudioLauncher.empty()) installedStudioLauncher = ExecutablePath();
        const fs::path studioLauncher = installedStudioLauncher.parent_path();
#ifdef _WIN32
        fs::path clientLauncherDirectory;
        const fs::path existingClientLauncher = FindNewestVersionedLauncher(
            installRoot / "launchers", kClientLauncherName);
        if (!existingClientLauncher.empty()) clientLauncherDirectory = existingClientLauncher.parent_path();
        if (hasRemoteManifest && !manifest.launcherVersion.empty()) {
            try {
                const fs::path clientLauncher = EnsureClientLauncherInstalled(manifest, hasRemoteManifest, installRoot);
                clientLauncherDirectory = clientLauncher.parent_path();
                RegisterBluboxProtocolForCurrentUser(clientLauncher);
                CreateBluboxClientShortcuts(clientLauncher);
            } catch (const std::exception &error) {
                // Updating the client association must not prevent Studio from
                // opening when the client launcher payload is unavailable.
                LogLauncherDiagnostic(std::string("Client protocol repair was skipped: ") + error.what());
            }
        }
#else
        const fs::path existingClientLauncher = FindNewestVersionedLauncher(
            installRoot / "launchers", kClientLauncherName);
        const fs::path clientLauncherDirectory = existingClientLauncher.empty()
            ? fs::path{} : existingClientLauncher.parent_path();
#endif
        PruneVersionDirectories(installRoot / "launchers",
            {ExecutablePath().parent_path(), studioLauncher, clientLauncherDirectory});
        LogLauncherDiagnostic("Starting Studio: " + studio.string());
        StartClient(studio, argc, argv);
        return 0;
    }

    // The game launcher bootstraps and repairs Studio before starting the game.
    // The dedicated Studio launcher uses the same feed but skips this bootstrap.
    // Studio is bundled with the platform, but a stale or unavailable Studio
    // release must not prevent an already-installed game client from starting.
    try {
        (void)EnsureStudioInstalled(manifest, hasRemoteManifest, installRoot, true);
    } catch (const std::exception &error) {
        LogLauncherDiagnostic(std::string("Studio bootstrap was skipped; continuing with the client: ") + error.what());
    }
    const fs::path installedLauncher = EnsureClientLauncherInstalled(manifest, hasRemoteManifest, installRoot);
    const fs::path installedStudioLauncher = FindNewestVersionedLauncher(
        installRoot / "launchers", kStudioLauncherName);
    const fs::path studioLauncher = installedStudioLauncher.empty()
        ? fs::path{} : installedStudioLauncher.parent_path();
    PruneVersionDirectories(installRoot / "launchers",
        {ExecutablePath().parent_path(), installedLauncher.parent_path(), studioLauncher});
#ifdef _WIN32
    // The browser protocol must always point to the verified launcher chosen
    // by this manifest, even if the client payload update later falls back.
    RegisterBluboxProtocolForCurrentUser(installedLauncher);
    CreateBluboxClientShortcuts(installedLauncher);
#endif

    const std::string targetVersion = manifest.version;
    const fs::path versionDirectory = installRoot / "versions" / targetVersion;
    fs::path installedClient = versionDirectory / kClientBinaryName;
    const bool remoteIsNewer = hasPreviousInstall && IsNewer(targetVersion, installedVersion);
    if (hasPreviousInstall && !remoteIsNewer) installedClient = previouslyInstalledClient;
    bool cachedClientMatchesManifest = fs::exists(installedClient);
    if (cachedClientMatchesManifest && hasRemoteManifest && targetVersion == installedVersion &&
        !manifest.sha256.empty()) {
        try {
            cachedClientMatchesManifest = Sha256File(installedClient) == manifest.sha256;
        } catch (const std::exception &) {
            cachedClientMatchesManifest = false;
        }
        if (!cachedClientMatchesManifest) {
            std::error_code removeError;
            fs::remove(installedClient, removeError);
        }
    }
    const bool needsUpdate = !cachedClientMatchesManifest || (hasRemoteManifest && remoteIsNewer);
    LogLauncherDiagnostic("Client update decision: target=" + targetVersion + ", installed=" + installedVersion +
        ", update=" + (needsUpdate ? "yes" : "no") + ".");

    if (needsUpdate) {
        SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_HIGHDPI);
        InitWindow(960, 594, "Blubox Installer");
        if (!IsWindowReady()) throw std::runtime_error("Could not open the update window.");
        SetTargetFPS(60);
        InitializeSplashWindow();
        fs::create_directories(versionDirectory);
        const fs::path stagingClient = versionDirectory / (kClientBinaryName.string() + ".tmp");
        try {
            fs::remove(stagingClient);
            if (hasRemoteManifest && !manifest.downloadUrl.empty()) {
#if defined(_WIN32) || defined(__APPLE__)
                LogLauncherDiagnostic("Downloading the client executable.");
                DownloadHttpsFile(manifest.downloadUrl, stagingClient);
                if (Sha256File(stagingClient) != manifest.sha256) {
                    throw std::runtime_error("The downloaded client SHA-256 checksum does not match.");
                }
                LogLauncherDiagnostic("Client download and SHA-256 verification completed.");
#endif
            } else {
                CopyWithProgress(launcherDirectory / kClientBinaryName, stagingClient);
            }
            fs::remove(installedClient);
            fs::rename(stagingClient, installedClient);
#ifdef __APPLE__
            fs::permissions(installedClient,
                fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                fs::perms::group_read | fs::perms::group_exec |
                fs::perms::others_read | fs::perms::others_exec,
                fs::perm_options::replace);
#endif
            DrawProgress(1.0f, "Starting Blubox...", 0, 0, 0.0);
            CloseSplashWindow();
        } catch (const std::exception &error) {
            CloseSplashWindow();
            std::error_code removeError;
            fs::remove(stagingClient, removeError);
            LogLauncherDiagnostic(std::string("Client update failed: ") + error.what());
            if (!hasPreviousInstall || !fs::exists(previouslyInstalledClient)) {
                throw std::runtime_error(std::string("The Blubox update could not be downloaded or verified, and no installed client is available. ") +
                    error.what() + " Please try again later or run the current setup from the official website.");
            }
            installedClient = previouslyInstalledClient;
            LogLauncherDiagnostic("Starting the last working client version " + installedVersion + " instead.");
#ifndef _WIN32
            std::cerr << "Blubox update failed; starting installed version " << installedVersion
                      << " instead: " << error.what() << '\n';
#endif
        } catch (...) {
            CloseSplashWindow();
            std::error_code removeError;
            fs::remove(stagingClient, removeError);
            throw;
        }
    }

    if (installedClient.parent_path() == versionDirectory) {
        fs::create_directories(installRoot);
        std::ofstream versionFile(installRoot / "installed_version.txt", std::ios::trunc);
        versionFile << targetVersion << '\n';
        if (!versionFile) throw std::runtime_error("Could not save the installed version.");
    }
    PruneVersionDirectories(installRoot / "versions", {installedClient.parent_path()});

#ifdef _WIN32
    RegisterBluboxClientUninstaller(installedLauncher, targetVersion);
#endif
    LogLauncherDiagnostic("Starting client: " + installedClient.string());
    StartClient(installedClient, argc, argv);
    return 0;
}
