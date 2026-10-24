#include "StudioBootstrap.hpp"

#include <raylib.h>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef BLUBOX_STUDIO_VERSION
#define BLUBOX_STUDIO_VERSION "0.1.0"
#endif
#ifndef BLUBOX_STUDIO_MANIFEST_URL
#define BLUBOX_STUDIO_MANIFEST_URL ""
#endif

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;
constexpr wchar_t kReadyArgument[] = L"--blubox-studio-ready";

struct HttpsUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
};

struct UpdateManifest {
    std::string version;
    std::string downloadUrl;
    std::string sha256;
};

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("Invalid UTF-8 in Studio update metadata.");
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count);
    return result;
}

HttpsUrl ParseHttpsUrl(std::wstring_view value) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    std::wstring copy(value);
    if (!WinHttpCrackUrl(copy.c_str(), static_cast<DWORD>(copy.size()), 0, &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS || components.dwHostNameLength == 0) {
        throw std::runtime_error("Studio update URLs must use HTTPS.");
    }
    HttpsUrl result;
    result.host.assign(components.lpszHostName, components.dwHostNameLength);
    result.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength) {
        result.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    result.port = components.nPort;
    return result;
}

std::vector<uint8_t> HttpGet(std::wstring_view url, const std::function<void(size_t, size_t)>& progress = {}) {
    const HttpsUrl parts = ParseHttpsUrl(url);
    HINTERNET session = WinHttpOpen(L"BluboxStudio/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) throw std::runtime_error("Could not initialize the Studio updater network session.");
    HINTERNET connection = WinHttpConnect(session, parts.host.c_str(), parts.port, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", parts.path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) {
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        throw std::runtime_error("Could not fetch the Studio update.");
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX) || status < 200 || status >= 300) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        throw std::runtime_error("The Studio update server returned an unsuccessful response.");
    }

    DWORD totalBytes = 0;
    DWORD totalSize = sizeof(totalBytes);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &totalBytes, &totalSize,
                        WINHTTP_NO_HEADER_INDEX);
    std::vector<uint8_t> bytes;
    std::array<uint8_t, 64 * 1024> buffer{};
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) break;
        if (available == 0) break;
        while (available > 0) {
            DWORD read = 0;
            const DWORD amount = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
            if (!WinHttpReadData(request, buffer.data(), amount, &read) || read == 0) {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                throw std::runtime_error("The Studio update download was interrupted.");
            }
            bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + read);
            available -= read;
            if (progress) progress(bytes.size(), totalBytes);
        }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return bytes;
}

bool IsVersion(std::string_view version) {
    if (version.empty() || version.size() > 32) return false;
    return std::all_of(version.begin(), version.end(), [](unsigned char c) {
        return std::isdigit(c) || c == '.';
    });
}

bool IsNewer(std::string_view candidate, std::string_view current) {
    std::istringstream left{std::string(candidate)};
    std::istringstream right{std::string(current)};
    std::string l, r;
    for (;;) {
        const bool hasLeft = static_cast<bool>(std::getline(left, l, '.'));
        const bool hasRight = static_cast<bool>(std::getline(right, r, '.'));
        if (!hasLeft && !hasRight) return false;
        const unsigned long lv = hasLeft ? std::stoul(l) : 0;
        const unsigned long rv = hasRight ? std::stoul(r) : 0;
        if (lv != rv) return lv > rv;
    }
}

UpdateManifest ParseManifest(std::string_view contents) {
    const json data = json::parse(contents);
    if (!data.is_object() || !data.contains("version") || !data["version"].is_string() ||
        !data.contains("downloadUrl") || !data["downloadUrl"].is_string() ||
        !data.contains("sha256") || !data["sha256"].is_string()) {
        throw std::runtime_error("Studio update manifest is missing required fields.");
    }
    UpdateManifest manifest{data["version"].get<std::string>(),
                            data["downloadUrl"].get<std::string>(),
                            data["sha256"].get<std::string>()};
    if (!IsVersion(manifest.version) || manifest.sha256.size() != 64 ||
        !std::all_of(manifest.sha256.begin(), manifest.sha256.end(), [](unsigned char c) {
            return std::isxdigit(c);
        })) throw std::runtime_error("Studio update manifest values are invalid.");
    return manifest;
}

std::string Sha256(std::span<const uint8_t> bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0, returned = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
                          sizeof(objectLength), &returned, 0) < 0) {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("Could not prepare Studio update verification.");
    }
    std::vector<uint8_t> object(objectLength);
    std::array<uint8_t, 32> digest{};
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("Could not verify the downloaded Studio update.");
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const uint8_t byte : digest) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

void DrawProgress(std::string_view status, size_t received, size_t total) {
    if (!IsWindowReady()) {
        SetConfigFlags(FLAG_WINDOW_UNDECORATED);
        InitWindow(620, 230, "Blubox Studio");
        SetTargetFPS(60);
    }
    if (WindowShouldClose()) throw std::runtime_error("Studio update cancelled.");
    BeginDrawing();
    ClearBackground(Color{19, 19, 26, 255});
    DrawText("BLOBOX STUDIO", 36, 34, 15, Color{0, 210, 255, 255});
    DrawText("Preparing your workspace", 36, 68, 28, Color{235, 238, 245, 255});
    DrawText(TextFormat("%.*s", static_cast<int>(status.size()), status.data()),
             36, 133, 16, Color{155, 164, 180, 255});
    DrawRectangle(36, 176, 548, 8, Color{41, 43, 53, 255});
    const float progress = total ? std::clamp(static_cast<float>(received) / total, 0.0f, 1.0f) : 0.12f;
    DrawRectangle(36, 176, static_cast<int>(548.0f * progress), 8, Color{0, 210, 255, 255});
    EndDrawing();
}

std::wstring QuoteArgument(std::wstring_view argument) {
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'"') { result.append(backslashes * 2 + 1, L'\\'); result.push_back(c); }
        else { result.append(backslashes, L'\\'); result.push_back(c); }
        backslashes = 0;
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

bool HasReadyFlag(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (std::string_view(argv[i]) == "--blubox-studio-ready") return true;
    return false;
}

void LaunchUpdated(const fs::path& executable, int argc, char** argv) {
    (void)argc;
    (void)argv;
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring command = QuoteArgument(executable.wstring());
    for (int i = 1; args && i < count; ++i) command += L" " + QuoteArgument(args[i]);
    command += L" ";
    command += kReadyArgument;
    if (args) LocalFree(args);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, executable.parent_path().c_str(), &startup, &process)) {
        throw std::runtime_error("Could not start the updated Studio.");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

fs::path CurrentExecutable() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("Could not locate Blubox Studio.");
    path.resize(length);
    return fs::path(path);
}
} // namespace
#endif

bool TryRunStudioUpdate(int argc, char** argv) {
#ifdef _WIN32
    if (HasReadyFlag(argc, argv) || std::string_view(BLUBOX_STUDIO_MANIFEST_URL).empty()) return false;
    try {
        const auto manifestBytes = HttpGet(Utf8ToWide(BLUBOX_STUDIO_MANIFEST_URL));
        const UpdateManifest manifest = ParseManifest(std::string_view(
            reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size()));
        if (!IsNewer(manifest.version, BLUBOX_STUDIO_VERSION)) return false;

        wchar_t localAppData[MAX_PATH]{};
        const DWORD chars = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
        if (!chars || chars >= MAX_PATH) throw std::runtime_error("LOCALAPPDATA is unavailable.");
        const fs::path current = CurrentExecutable();
        const fs::path versionDirectory = fs::path(localAppData) / L"Blubox" / L"Studio" /
            L"versions" / Utf8ToWide(manifest.version);
        fs::create_directories(versionDirectory / L"assets" / L"editor" / L"fonts");
        const fs::path executable = versionDirectory / L"BluboxStudio.exe";

        // A desktop or protocol shortcut can still point to the installer copy.
        // Reuse a previously verified versioned update instead of downloading
        // it again on every launch from that older bootstrap copy.
        bool cachedUpdateIsValid = false;
        if (fs::exists(executable)) {
            std::ifstream cachedFile(executable, std::ios::binary);
            if (cachedFile) {
                std::vector<uint8_t> cachedBytes((std::istreambuf_iterator<char>(cachedFile)),
                                                std::istreambuf_iterator<char>());
                cachedUpdateIsValid = !cachedFile.bad() && !cachedBytes.empty() &&
                    _stricmp(Sha256(cachedBytes).c_str(), manifest.sha256.c_str()) == 0;
            }
            if (!cachedUpdateIsValid) fs::remove(executable);
        }

        if (!cachedUpdateIsValid) {
            const auto updateBytes = HttpGet(Utf8ToWide(manifest.downloadUrl),
                [](size_t received, size_t total) { DrawProgress("Downloading Studio update", received, total); });
            if (_stricmp(Sha256(updateBytes).c_str(), manifest.sha256.c_str()) != 0) {
                throw std::runtime_error("The Studio update checksum did not match.");
            }

            DrawProgress("Installing update", updateBytes.size(), updateBytes.size());
            const fs::path staging = versionDirectory / L"BluboxStudio.exe.tmp";
            {
                std::ofstream output(staging, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("Could not stage the Studio update.");
                output.write(reinterpret_cast<const char*>(updateBytes.data()),
                             static_cast<std::streamsize>(updateBytes.size()));
                if (!output) throw std::runtime_error("Could not save the Studio update.");
            }
            fs::remove(executable);
            fs::rename(staging, executable);
        }

        const fs::path sourceFonts = current.parent_path() / L"assets" / L"editor" / L"fonts";
        fs::copy(sourceFonts, versionDirectory / L"assets" / L"editor" / L"fonts",
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        if (IsWindowReady()) CloseWindow();
        LaunchUpdated(executable, argc, argv);
        return true;
    } catch (const std::exception& error) {
        if (IsWindowReady()) CloseWindow();
        OutputDebugStringA((std::string("Blubox Studio updater: ") + error.what() + "\n").c_str());
        return false;
    }
#else
    (void)argc;
    (void)argv;
    return false;
#endif
}
