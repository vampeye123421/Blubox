#include "StudioBootstrap.hpp"
#include "Diagnostics.hpp"

#include <raylib.h>
#include <nlohmann/json.hpp>

#if defined(_WIN32) || defined(__APPLE__)
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
#include <bcrypt.h>
#include <shellapi.h>
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle
#elif defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/stat.h>
extern char** environ;
#endif

#include "HttpClient.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
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
constexpr char kReadyArgument[] = "--blubox-studio-ready";

struct UpdateManifest {
    std::string version;
    std::string downloadUrl;
    std::string sha256;
};

std::vector<uint8_t> HttpGet(std::string_view url, const std::function<void(size_t, size_t)>& progress = {}) {
    if (!url.starts_with("https://")) throw std::runtime_error("Studio update URLs must use HTTPS.");
    blubox::platform::HttpRequest request;
    request.url = std::string(url);
    request.timeoutMs = 120000;
    request.maxResponseBytes = 512u * 1024u * 1024u;
    std::vector<uint8_t> bytes;
    request.onChunk = [&bytes](const char* data, std::size_t size) {
        constexpr std::size_t maximum = 512u * 1024u * 1024u;
        if (size > maximum - std::min(bytes.size(), maximum)) {
            throw std::runtime_error("The Studio update exceeds the supported download size.");
        }
        bytes.insert(bytes.end(), data, data + size);
    };
    if (progress) {
        request.onProgress = [&progress](std::uint64_t received, std::uint64_t total) {
            progress(static_cast<std::size_t>(received), static_cast<std::size_t>(total));
        };
    }
    const auto response = blubox::platform::PerformHttpRequest(request);
    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("The Studio update server returned HTTP " +
                                 std::to_string(response.status) + ".");
    }
    return bytes;
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
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
    if (!data.is_object() || !data.contains("version") || !data["version"].is_string()) {
        throw std::runtime_error("Studio update manifest is missing required fields.");
    }
    const json* platformData = &data;
#ifdef __APPLE__
    if (!data.contains("platforms") || !data["platforms"].is_object() ||
        !data["platforms"].contains("macos") || !data["platforms"]["macos"].is_object()) {
        throw std::runtime_error("Studio update manifest does not contain macOS download metadata.");
    }
    platformData = &data["platforms"]["macos"];
#endif
    if (!platformData->contains("downloadUrl") ||
        !(*platformData)["downloadUrl"].is_string() ||
        !platformData->contains("sha256") ||
        !(*platformData)["sha256"].is_string()) {
        throw std::runtime_error("Studio update manifest is missing required platform fields.");
    }
    UpdateManifest manifest{data["version"].get<std::string>(),
                            (*platformData)["downloadUrl"].get<std::string>(),
                            (*platformData)["sha256"].get<std::string>()};
    if (!IsVersion(manifest.version) || manifest.sha256.size() != 64 ||
        !std::all_of(manifest.sha256.begin(), manifest.sha256.end(), [](unsigned char c) {
            return std::isxdigit(c);
        })) throw std::runtime_error("Studio update manifest values are invalid.");
    return manifest;
}

std::string Sha256(std::span<const uint8_t> bytes) {
#ifdef _WIN32
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
#else
    if (bytes.size() > std::numeric_limits<CC_LONG>::max()) {
        throw std::runtime_error("The Studio update is too large to verify.");
    }
    std::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest{};
    if (CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data()) == nullptr) {
        throw std::runtime_error("Could not verify the downloaded Studio update.");
    }
#endif
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

#ifdef _WIN32
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
#endif

bool HasReadyFlag(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (std::string_view(argv[i]) == "--blubox-studio-ready") return true;
    return false;
}

void LaunchUpdated(const fs::path& executable, int argc, char** argv) {
#ifdef _WIN32
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring command = QuoteArgument(executable.wstring());
    for (int i = 1; args && i < count; ++i) command += L" " + QuoteArgument(args[i]);
    command += L" --blubox-studio-ready";
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
#else
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc) + 1);
    arguments.push_back(executable.string());
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) != kReadyArgument) arguments.emplace_back(argv[i]);
    }
    arguments.emplace_back(kReadyArgument);
    std::vector<char*> argumentPointers;
    argumentPointers.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argumentPointers.push_back(argument.data());
    argumentPointers.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        throw std::runtime_error("Could not prepare the updated Studio process.");
    }
    const int chdirResult = posix_spawn_file_actions_addchdir_np(
        &actions, executable.parent_path().c_str());
    pid_t process = 0;
    const int result = chdirResult == 0
        ? posix_spawn(&process, executable.c_str(), &actions, nullptr, argumentPointers.data(), environ)
        : chdirResult;
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) throw std::runtime_error("Could not start the updated Studio.");
#endif
}

fs::path CurrentExecutable() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("Could not locate Blubox Studio.");
    path.resize(length);
    return fs::path(path);
#else
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        throw std::runtime_error("Could not locate Blubox Studio.");
    }
    std::error_code error;
    const fs::path result = fs::canonical(path.c_str(), error);
    if (error) throw std::runtime_error("Could not locate Blubox Studio: " + error.message());
    return result;
#endif
}
} // namespace
#endif

bool TryRunStudioUpdate(int argc, char** argv) {
#if defined(_WIN32) || defined(__APPLE__)
    if (HasReadyFlag(argc, argv)) {
        blubox::studio::LogDiagnostic("INFO", "Updater handoff accepted; starting the verified Studio build.");
        return false;
    }
    if (std::string_view(BLUBOX_STUDIO_MANIFEST_URL).empty()) {
        blubox::studio::LogDiagnostic("WARNING", "Updater skipped because no manifest URL is configured.");
        return false;
    }
    try {
        blubox::studio::LogDiagnostic("INFO", "Checking the Studio update manifest.");
        const auto manifestBytes = HttpGet(BLUBOX_STUDIO_MANIFEST_URL);
        const UpdateManifest manifest = ParseManifest(std::string_view(
            reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size()));
        if (!IsNewer(manifest.version, BLUBOX_STUDIO_VERSION)) {
            blubox::studio::LogDiagnostic("INFO", "Studio is current at version " BLUBOX_STUDIO_VERSION ".");
            return false;
        }
        blubox::studio::LogDiagnostic("INFO", std::string("Studio update available: ") + BLUBOX_STUDIO_VERSION +
            " -> " + manifest.version + ".");

#ifdef _WIN32
        wchar_t localAppData[MAX_PATH]{};
        const DWORD chars = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
        if (!chars || chars >= MAX_PATH) throw std::runtime_error("LOCALAPPDATA is unavailable.");
        const fs::path appData(localAppData);
#else
        const char* home = std::getenv("HOME");
        if (home == nullptr || *home == '\0') throw std::runtime_error("The current user's home folder is unavailable.");
        const fs::path appData = fs::path(home) / "Library" / "Application Support";
#endif
        const fs::path current = CurrentExecutable();
        const fs::path versionDirectory = appData / "Blubox" / "studio" /
            "versions" / manifest.version;
        fs::create_directories(versionDirectory / "assets" / "editor" / "fonts");
#ifdef _WIN32
        const fs::path executable = versionDirectory / "BluboxStudio.exe";
#else
        const fs::path executable = versionDirectory / "blubox_studio";
#endif

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
                    Lowercase(Sha256(cachedBytes)) == Lowercase(manifest.sha256);
            }
            if (!cachedUpdateIsValid) fs::remove(executable);
        }

        if (!cachedUpdateIsValid) {
            blubox::studio::LogDiagnostic("INFO", "Downloading and verifying Studio update.");
            const auto updateBytes = HttpGet(manifest.downloadUrl,
                [](size_t received, size_t total) { DrawProgress("Downloading Studio update", received, total); });
            if (Lowercase(Sha256(updateBytes)) != Lowercase(manifest.sha256)) {
                throw std::runtime_error("The Studio update checksum did not match.");
            }

            DrawProgress("Installing update", updateBytes.size(), updateBytes.size());
            const fs::path staging = executable.string() + ".tmp";
            {
                std::ofstream output(staging, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("Could not stage the Studio update.");
                output.write(reinterpret_cast<const char*>(updateBytes.data()),
                             static_cast<std::streamsize>(updateBytes.size()));
                if (!output) throw std::runtime_error("Could not save the Studio update.");
            }
            fs::remove(executable);
            fs::rename(staging, executable);
#ifdef __APPLE__
            if (chmod(executable.c_str(), S_IRUSR | S_IWUSR | S_IXUSR |
                      S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
                throw std::runtime_error("Could not make the Studio update executable.");
            }
#endif
            blubox::studio::LogDiagnostic("INFO", "Studio executable installed in the versioned cache.");
        }

        const fs::path sourceFonts = current.parent_path() / "assets" / "editor" / "fonts";
        fs::copy(sourceFonts, versionDirectory / "assets" / "editor" / "fonts",
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        if (IsWindowReady()) CloseWindow();
        blubox::studio::LogDiagnostic("INFO", "Starting the verified Studio update process.");
        LaunchUpdated(executable, argc, argv);
        return true;
    } catch (const std::exception& error) {
        if (IsWindowReady()) CloseWindow();
        blubox::studio::LogDiagnostic("ERROR", std::string("Studio updater failed: ") + error.what());
#ifdef _WIN32
        OutputDebugStringA((std::string("Blubox Studio updater: ") + error.what() + "\n").c_str());
#endif
        return false;
    }
#else
    (void)argc;
    (void)argv;
    return false;
#endif
}
