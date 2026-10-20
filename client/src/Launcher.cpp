#include "Launcher.hpp"

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
#include <iterator>
#include <memory>
#include <optional>
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
};

void DrawProgress(float progress, std::string_view status);

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
    if (length <= 0) throw std::runtime_error("URL update non valida (UTF-8).");
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
        throw std::runtime_error("Il manifest deve usare un URL HTTPS valido.");
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
    if (!session.value) throw std::runtime_error("WinHTTP non disponibile.");
    WinHttpSetTimeouts(session.value, 8000, 8000, 15000, 15000);
    WinHttpHandle connection;
    connection.value = WinHttpConnect(session.value, url.host.c_str(), url.port, 0);
    if (!connection.value) throw std::runtime_error("Connessione HTTPS al feed non riuscita.");
    WinHttpHandle request;
    request.value = WinHttpOpenRequest(connection.value, L"GET", url.path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.flags);
    if (!request.value) throw std::runtime_error("Richiesta HTTPS non riuscita.");
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        throw std::runtime_error("Download HTTPS non riuscito.");
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX) || status != 200) {
        throw std::runtime_error("Il server update ha restituito HTTP " + std::to_string(status) + ".");
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
            throw std::runtime_error("Download HTTPS interrotto.");
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
            throw std::runtime_error("Manifest troppo grande.");
        }
        body.append(data, count);
    });
    return body;
}

void DownloadHttpsFile(std::string_view url, const fs::path &destination) {
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Impossibile creare il file temporaneo del client.");
    auto nextProgressDraw = std::chrono::steady_clock::now();
    TransferHttps(CrackHttpsUrl(url), [&output, &nextProgressDraw](const char *data, size_t count,
                                                                   ULONGLONG total, ULONGLONG received) {
        output.write(data, static_cast<std::streamsize>(count));
        if (!output) throw std::runtime_error("Errore durante il salvataggio del client scaricato.");
        const float progress = total > 0 ? std::min(1.0f, static_cast<float>(received) /
                                                        static_cast<float>(total)) : 0.0f;
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextProgressDraw || (total > 0 && received >= total)) {
            DrawProgress(progress, "Downloading update...");
            nextProgressDraw = now + std::chrono::milliseconds(100);
        }
        if (WindowShouldClose()) throw std::runtime_error("Aggiornamento annullato.");
    });
    output.close();
    if (!output) throw std::runtime_error("Errore durante la chiusura del client scaricato.");
}

std::string Sha256File(const fs::path &path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        throw std::runtime_error("Impossibile inizializzare SHA-256.");
    }
    try {
        DWORD objectSize = 0;
        DWORD resultSize = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                              &resultSize, 0) < 0) throw std::runtime_error("Errore SHA-256.");
        std::vector<UCHAR> object(objectSize);
        if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) < 0) {
            throw std::runtime_error("Impossibile creare l'hash SHA-256.");
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Impossibile aprire il client per la verifica.");
        std::array<char, 256 * 1024> buffer{};
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                                            static_cast<ULONG>(count), 0) < 0) {
                throw std::runtime_error("Verifica SHA-256 non riuscita.");
            }
        }
        if (!input.eof()) throw std::runtime_error("Errore di lettura durante la verifica SHA-256.");
        std::array<UCHAR, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
            throw std::runtime_error("Finalizzazione SHA-256 non riuscita.");
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
    if (length == 0 || length >= buffer.size()) throw std::runtime_error("Impossibile trovare il launcher.");
    buffer.resize(length);
    return fs::path(buffer);
#else
    std::error_code error;
    const fs::path path = fs::canonical("/proc/self/exe", error);
    if (error) throw std::runtime_error("Impossibile trovare il launcher: " + error.message());
    return path;
#endif
}

fs::path InstallRoot() {
#ifdef _WIN32
    const wchar_t *localAppData = _wgetenv(L"LOCALAPPDATA");
    if (localAppData == nullptr || *localAppData == L'\0') throw std::runtime_error("LOCALAPPDATA non e' definita.");
    return fs::path(localAppData) / L"Blubox";
#else
    const char *home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') throw std::runtime_error("HOME non e' definita.");
    return fs::path(home) / ".local" / "share" / "Blubox";
#endif
}

std::string ReadManifestVersion(const fs::path &manifest) {
    std::ifstream input(manifest);
    if (!input) throw std::runtime_error("Manifest non trovato: " + manifest.string());
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::string key = "\"version\"";
    const size_t keyAt = contents.find(key);
    if (keyAt == std::string::npos) throw std::runtime_error("Il manifest non contiene il campo version.");
    const size_t colon = contents.find(':', keyAt + key.size());
    const size_t openQuote = contents.find('"', colon == std::string::npos ? keyAt : colon + 1);
    const size_t closeQuote = contents.find('"', openQuote == std::string::npos ? keyAt : openQuote + 1);
    if (colon == std::string::npos || openQuote == std::string::npos || closeQuote == std::string::npos) {
        throw std::runtime_error("Versione non valida nel manifest.");
    }
    const std::string version = contents.substr(openQuote + 1, closeQuote - openQuote - 1);
    if (version.empty() || !std::all_of(version.begin(), version.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '-' || c == '_';
        })) throw std::runtime_error("Il valore version contiene caratteri non validi.");
    return version;
}

UpdateManifest ParseUpdateManifest(std::string_view contents) {
    nlohmann::json json;
    try { json = nlohmann::json::parse(contents); }
    catch (const std::exception &) { throw std::runtime_error("Manifest update JSON non valido."); }
    if (!json.is_object() || !json.contains("version") || !json["version"].is_string()) {
        throw std::runtime_error("Manifest update senza versione valida.");
    }
    UpdateManifest manifest;
    manifest.version = json["version"].get<std::string>();
    if (manifest.version.empty() || !std::all_of(manifest.version.begin(), manifest.version.end(),
            [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-' || c == '_'; })) {
        throw std::runtime_error("Versione non valida nel manifest update.");
    }
    if (json.contains("downloadUrl") && json["downloadUrl"].is_string()) {
        manifest.downloadUrl = json["downloadUrl"].get<std::string>();
    }
    if (json.contains("sha256") && json["sha256"].is_string()) {
        manifest.sha256 = json["sha256"].get<std::string>();
    }
    if (!manifest.downloadUrl.empty() || !manifest.sha256.empty()) {
        if (manifest.downloadUrl.empty() || manifest.sha256.size() != 64 ||
            !std::all_of(manifest.sha256.begin(), manifest.sha256.end(), [](unsigned char c) {
                return std::isxdigit(c) != 0;
            })) throw std::runtime_error("URL o SHA-256 non validi nel manifest update.");
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

void DrawProgress(float progress, std::string_view status) {
    constexpr int width = 520;
    const int left = (GetScreenWidth() - width) / 2;
    BeginDrawing();
    ClearBackground(Color{17, 22, 32, 255});
    DrawText("BLUBOX", left, GetScreenHeight() / 2 - 94, 28, RAYWHITE);
    DrawText(TextFormat("%.*s", static_cast<int>(status.size()), status.data()), left,
             GetScreenHeight() / 2 - 48, 18, Color{184, 196, 214, 255});
    DrawRectangle(left, GetScreenHeight() / 2, width, 12, Color{48, 58, 76, 255});
    DrawRectangle(left, GetScreenHeight() / 2, static_cast<int>(width * progress), 12,
                  Color{80, 166, 255, 255});
    DrawText(TextFormat("%d%%", static_cast<int>(progress * 100.0f)), left,
             GetScreenHeight() / 2 + 24, 16, RAYWHITE);
    EndDrawing();
}

void CopyWithProgress(const fs::path &source, const fs::path &destination) {
    std::ifstream input(source, std::ios::binary);
    if (!input) throw std::runtime_error("Payload client non trovato accanto al launcher: " + source.string());
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Impossibile creare il file temporaneo del client.");
    std::error_code sizeError;
    const auto totalBytes = fs::file_size(source, sizeError);
    std::array<char, 256 * 1024> buffer{};
    std::uintmax_t copied = 0;
    bool windowOpen = false;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            output.write(buffer.data(), count);
            if (!output) throw std::runtime_error("Errore durante la copia del client.");
            copied += static_cast<std::uintmax_t>(count);
        }
        const float progress = !sizeError && totalBytes > 0 ? std::min(1.0f, static_cast<float>(copied) / static_cast<float>(totalBytes)) : 0.0f;
        DrawProgress(progress, "Downloading update...");
        windowOpen = true;
        if (WindowShouldClose()) throw std::runtime_error("Aggiornamento annullato.");
    }
    output.close();
    if (!input.eof()) throw std::runtime_error("Errore durante la lettura del client locale.");
    if (windowOpen) DrawProgress(1.0f, "Installing update...");
}

#ifdef _WIN32
std::wstring NarrowArgToWide(const char *text) {
    if (text == nullptr) return {};
    // Narrow argv on Windows uses the active code page; round-trip those bytes
    // back to UTF-16 before CreateProcessW rebuilds the argument vector.
    const int needed = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (needed <= 0) throw std::runtime_error("Argomento di avvio non convertibile in Unicode.");
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
        throw std::runtime_error("Avvio del client non riuscito (codice " + std::to_string(GetLastError()) + ").");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}
#else
void StartClient(const fs::path &client, int argc, char *argv[]) {
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("Impossibile creare il processo client.");
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

} // namespace

int RunLauncher(const LaunchOptions &, int argc, char *argv[]) {
    const fs::path launcherDirectory = ExecutablePath().parent_path();
    const fs::path installRoot = InstallRoot();
    std::ifstream installedVersionFile(installRoot / "installed_version.txt");
    std::string installedVersion;
    std::getline(installedVersionFile, installedVersion);
    const fs::path previouslyInstalledClient = installRoot / "versions" / installedVersion / "blubox_client.exe";
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

    const fs::path versionDirectory = installRoot / "versions" / manifest.version;
    fs::path installedClient = versionDirectory / "blubox_client.exe";
    const bool remoteIsNewer = hasPreviousInstall && IsNewer(manifest.version, installedVersion);
    if (hasPreviousInstall && !remoteIsNewer) installedClient = previouslyInstalledClient;
    const bool needsUpdate = !fs::exists(installedClient);

    if (needsUpdate) {
        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
        InitWindow(680, 320, "Blubox Launcher");
        if (!IsWindowReady()) throw std::runtime_error("Impossibile inizializzare la finestra di aggiornamento.");
        SetTargetFPS(60);
        fs::create_directories(versionDirectory);
        const fs::path stagingClient = versionDirectory / "blubox_client.exe.tmp";
        try {
            fs::remove(stagingClient);
            if (hasRemoteManifest && !manifest.downloadUrl.empty()) {
#ifdef _WIN32
                DownloadHttpsFile(manifest.downloadUrl, stagingClient);
                if (Sha256File(stagingClient) != manifest.sha256) {
                    throw std::runtime_error("Il checksum SHA-256 del client scaricato non corrisponde.");
                }
#else
                throw std::runtime_error("Download automatico disponibile solo su Windows.");
#endif
            } else {
                CopyWithProgress(launcherDirectory / "blubox_client.exe", stagingClient);
            }
            fs::remove(installedClient);
            fs::rename(stagingClient, installedClient);
            DrawProgress(1.0f, "Starting Blubox...");
            CloseWindow();
        } catch (...) {
            CloseWindow();
            throw;
        }
    }

    if (installedClient.parent_path() == versionDirectory) {
        fs::create_directories(installRoot);
        std::ofstream versionFile(installRoot / "installed_version.txt", std::ios::trunc);
        versionFile << manifest.version << '\n';
        if (!versionFile) throw std::runtime_error("Impossibile salvare la versione installata.");
    }

    StartClient(installedClient, argc, argv);
    return 0;
}
