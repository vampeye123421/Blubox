#include "WindowsProtocolRegistration.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace {

constexpr wchar_t kProtocolRegistryPath[] = L"Software\\Classes\\blubox";
constexpr wchar_t kStudioProtocolRegistryPath[] = L"Software\\Classes\\blubox-studio";
constexpr wchar_t kUninstallRegistryRoot[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";

class RegistryKey {
public:
    RegistryKey() = default;
    ~RegistryKey() {
        if (handle_ != nullptr) RegCloseKey(handle_);
    }

    RegistryKey(const RegistryKey &) = delete;
    RegistryKey &operator=(const RegistryKey &) = delete;

    HKEY *Put() { return &handle_; }
    HKEY Get() const { return handle_; }

private:
    HKEY handle_ = nullptr;
};

[[noreturn]] void ThrowRegistryError(const char *operation, LSTATUS status) {
    throw std::runtime_error(std::string(operation) + " failed: " +
                             std::system_category().message(static_cast<int>(status)));
}

void SetStringValue(HKEY key, const wchar_t *valueName, const std::wstring &value) {
    const DWORD byteCount = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(
        key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()), byteCount);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Registry write", status);
}

std::wstring QuoteCommandArgument(const std::wstring &value) {
    return L"\"" + value + L"\"";
}

std::filesystem::path KnownFolderPath(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &path);
    if (FAILED(result)) throw std::runtime_error("A Windows known-folder path is unavailable.");
    std::filesystem::path output(path);
    CoTaskMemFree(path);
    return output;
}

void SaveShortcut(const std::filesystem::path &launcherExecutable,
                  const std::filesystem::path &shortcutPath, const wchar_t *description) {
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
        throw std::runtime_error("Could not initialize COM for shortcuts.");
    }
    IShellLinkW *link = nullptr;
    IPersistFile *persist = nullptr;
    try {
        HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&link));
        if (FAILED(result)) throw std::runtime_error("Could not create the shortcut.");
        const std::wstring target = launcherExecutable.wstring();
        const std::wstring workingDirectory = launcherExecutable.parent_path().wstring();
        if (FAILED(link->SetPath(target.c_str())) ||
            FAILED(link->SetWorkingDirectory(workingDirectory.c_str())) ||
            FAILED(link->SetDescription(description))) {
            throw std::runtime_error("Could not configure the shortcut.");
        }
        result = link->QueryInterface(IID_PPV_ARGS(&persist));
        if (FAILED(result)) throw std::runtime_error("Shortcut saving is unavailable.");
        result = persist->Save(shortcutPath.c_str(), TRUE);
        if (FAILED(result)) throw std::runtime_error("Could not save the shortcut.");
    } catch (...) {
        if (persist != nullptr) persist->Release();
        if (link != nullptr) link->Release();
        if (SUCCEEDED(initResult)) CoUninitialize();
        throw;
    }
    if (persist != nullptr) persist->Release();
    if (link != nullptr) link->Release();
    if (SUCCEEDED(initResult)) CoUninitialize();
}

void CreateAppShortcuts(const std::filesystem::path &launcherExecutable,
                        const wchar_t *shortcutName, const wchar_t *description) {
    const std::filesystem::path desktop = KnownFolderPath(FOLDERID_Desktop);
    const std::filesystem::path programs = KnownFolderPath(FOLDERID_Programs);
    const std::wstring groupName = std::wstring(shortcutName) == L"Blubox"
        ? L"Blubox Client" : L"Blubox Studio";
    SaveShortcut(launcherExecutable, desktop / (std::wstring(shortcutName) + L".lnk"), description);
    std::error_code error;
    std::filesystem::remove(programs / (std::wstring(shortcutName) + L".lnk"), error);
    std::filesystem::create_directories(programs / groupName, error);
    SaveShortcut(launcherExecutable, programs / groupName / (std::wstring(shortcutName) + L".lnk"), description);
}

void DeleteAppShortcuts(const wchar_t *shortcutName) {
    for (const KNOWNFOLDERID *folder : {&FOLDERID_Desktop, &FOLDERID_Programs}) {
        try {
            std::error_code error;
            const std::filesystem::path base = KnownFolderPath(*folder);
            std::filesystem::remove(base / (std::wstring(shortcutName) + L".lnk"), error);
            error.clear();
            const std::wstring groupName = std::wstring(shortcutName) == L"Blubox"
                ? L"Blubox Client" : L"Blubox Studio";
            std::filesystem::remove(base / groupName / (std::wstring(shortcutName) + L".lnk"), error);
            if (folder == &FOLDERID_Programs) {
                error.clear();
                std::filesystem::remove(base / (std::wstring(shortcutName) + L".lnk"), error);
            }
        } catch (...) {
            // Missing shell folders should not block uninstalling the binaries.
        }
    }
}

void RemoveUninstaller(const wchar_t *keyName) {
    const std::wstring subkey = std::wstring(kUninstallRegistryRoot) + keyName;
    const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        ThrowRegistryError("Uninstaller registration removal", status);
    }
}

std::wstring PowerShellLiteral(const std::wstring &value) {
    std::wstring result = L"'";
    for (wchar_t ch : value) {
        if (ch == L'\'') result += L"''";
        else if (ch == L'\r' || ch == L'\n') throw std::runtime_error("The installation path is invalid.");
        else result += ch;
    }
    result += L"'";
    return result;
}

void ScheduleFileCleanup(const std::filesystem::path &installRoot, bool studio) {
    std::vector<std::filesystem::path> targets;
    targets.push_back(installRoot / (studio ? L"studio" : L"versions"));
    targets.push_back(installRoot / (studio ? L"installed_studio_version.txt" : L"installed_version.txt"));
    const std::filesystem::path launchers = installRoot / L"launchers";
    std::error_code error;
    if (std::filesystem::exists(launchers, error)) {
        for (std::filesystem::recursive_directory_iterator it(launchers, error), end;
             !error && it != end; it.increment(error)) {
            if (it->is_regular_file(error) && it->path().filename() ==
                (studio ? L"blubox_studio_launcher.exe" : L"blubox_launcher.exe")) {
                targets.push_back(it->path());
            }
        }
    }

    const DWORD pid = GetCurrentProcessId();
    const std::filesystem::path script = std::filesystem::temp_directory_path() /
        (L"blubox-uninstall-" + std::to_wstring(pid) + L".ps1");
    std::wstring scriptBody = L"$ErrorActionPreference = 'SilentlyContinue'\ntry { Wait-Process -Id ";
    scriptBody += std::to_wstring(pid);
    scriptBody += L" -Timeout 90 } catch {}\n";
    for (const auto &target : targets) {
        scriptBody += L"Remove-Item -LiteralPath ";
        scriptBody += PowerShellLiteral(target.wstring());
        scriptBody += L" -Recurse -Force -ErrorAction SilentlyContinue\n";
    }
    scriptBody += L"Get-ChildItem -LiteralPath ";
    scriptBody += PowerShellLiteral(launchers.wstring());
    scriptBody += L" -Directory -ErrorAction SilentlyContinue | Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } | Remove-Item -Force -ErrorAction SilentlyContinue\n"
                  L"Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\n";
    const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, scriptBody.data(),
        static_cast<int>(scriptBody.size()), nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0) throw std::runtime_error("Could not encode the uninstaller script.");
    std::string utf8Script(static_cast<size_t>(utf8Size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, scriptBody.data(), static_cast<int>(scriptBody.size()),
                        utf8Script.data(), utf8Size, nullptr, nullptr);
    std::ofstream output(script, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Could not prepare to remove Blubox files.");
    output.write("\xEF\xBB\xBF", 3);
    output.write(utf8Script.data(), static_cast<std::streamsize>(utf8Script.size()));
    output.close();
    if (!output) throw std::runtime_error("Could not write the uninstaller script.");

    std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ";
    command += QuoteCommandArgument(script.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
        std::filesystem::remove(script, error);
        throw std::runtime_error("Could not start deferred cleanup.");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

std::wstring GetCurrentExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0) {
        ThrowRegistryError("Reading the executable path", GetLastError());
    }
    if (length >= path.size()) {
        throw std::runtime_error("The executable path is too long to register blubox://.");
    }
    path.resize(length);
    return path;
}

} // namespace
#endif

void RegisterBluboxProtocolForCurrentUser() {
    RegisterBluboxProtocolForCurrentUser(std::filesystem::path(GetCurrentExecutablePath()));
}

void RegisterBluboxProtocolForCurrentUser(const std::filesystem::path &launcherExecutable) {
#ifdef _WIN32
    const std::wstring executablePath = launcherExecutable.wstring();

    RegistryKey protocolKey;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, kProtocolRegistryPath, 0, nullptr,
                                     REG_OPTION_NON_VOLATILE,
                                     KEY_SET_VALUE | KEY_CREATE_SUB_KEY, nullptr,
                                     protocolKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creating the blubox registry key", status);

    SetStringValue(protocolKey.Get(), nullptr, L"URL:Blubox Protocol");
    SetStringValue(protocolKey.Get(), L"URL Protocol", L"");

    RegistryKey iconKey;
    status = RegCreateKeyExW(protocolKey.Get(), L"DefaultIcon", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                             iconKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creating the blubox icon key", status);
    std::wstring icon = L"\"";
    icon += executablePath;
    icon += L"\",0";
    SetStringValue(iconKey.Get(), nullptr, icon);

    RegistryKey commandKey;
    status = RegCreateKeyExW(protocolKey.Get(), L"shell\\open\\command", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                             commandKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creating the blubox command key", status);

    std::wstring command = L"\"";
    command += executablePath;
    command += L"\" \"%1\"";
    SetStringValue(commandKey.Get(), nullptr, command);
#else
    throw std::runtime_error("Registering the blubox:// protocol is available only on Windows.");
#endif
}

void UnregisterBluboxProtocolForCurrentUser() {
#ifdef _WIN32
    LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, kProtocolRegistryPath);
    if (status == ERROR_FILE_NOT_FOUND) return;
    if (status != ERROR_SUCCESS) ThrowRegistryError("Removing the blubox registration", status);

    status = RegDeleteKeyW(HKEY_CURRENT_USER, kProtocolRegistryPath);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        ThrowRegistryError("Removing the blubox registry key", status);
    }
#else
    throw std::runtime_error("Removing the blubox:// protocol registration is available only on Windows.");
#endif
}

void RegisterBluboxStudioProtocolForCurrentUser(const std::filesystem::path &studioExecutable) {
#ifdef _WIN32
    const std::wstring executablePath = studioExecutable.wstring();
    RegistryKey protocolKey;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, kStudioProtocolRegistryPath, 0, nullptr,
                                     REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_CREATE_SUB_KEY,
                                     nullptr, protocolKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creating the blubox-studio registry key", status);
    SetStringValue(protocolKey.Get(), nullptr, L"URL:Blubox Studio Protocol");
    SetStringValue(protocolKey.Get(), L"URL Protocol", L"");

    RegistryKey iconKey;
    status = RegCreateKeyExW(protocolKey.Get(), L"DefaultIcon", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                             iconKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creating the blubox-studio icon key", status);
    SetStringValue(iconKey.Get(), nullptr, L"\"" + executablePath + L"\",0");

    RegistryKey commandKey;
    status = RegCreateKeyExW(protocolKey.Get(), L"shell\\open\\command", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                             commandKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creating the blubox-studio command key", status);
    SetStringValue(commandKey.Get(), nullptr, L"\"" + executablePath + L"\" \"%1\"");
#else
    (void)studioExecutable;
    throw std::runtime_error("Registering the blubox-studio:// protocol is available only on Windows.");
#endif
}

void CreateBluboxStudioStartMenuShortcut(const std::filesystem::path &launcherExecutable) {
#ifdef _WIN32
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
        throw std::runtime_error("Could not initialize COM for the Studio shortcut.");
    }
    IShellLinkW *link = nullptr;
    IPersistFile *persist = nullptr;
    PWSTR programsFolder = nullptr;
    try {
        HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&link));
        if (FAILED(result)) throw std::runtime_error("Could not create the Studio shortcut.");
        const std::wstring path = launcherExecutable.wstring();
        if (FAILED(link->SetPath(path.c_str())) ||
            FAILED(link->SetWorkingDirectory(launcherExecutable.parent_path().wstring().c_str())) ||
            FAILED(link->SetDescription(L"Install and launch Blubox Studio"))) {
            throw std::runtime_error("Could not configure the Studio shortcut.");
        }
        result = link->QueryInterface(IID_PPV_ARGS(&persist));
        if (FAILED(result)) throw std::runtime_error("Studio shortcut saving is unavailable.");
        result = SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_CREATE, nullptr, &programsFolder);
        if (FAILED(result)) throw std::runtime_error("The Start Menu path is unavailable.");
        const std::filesystem::path shortcut = std::filesystem::path(programsFolder) /
            L"Blubox Studio" / L"Blubox Studio.lnk";
        std::filesystem::create_directories(shortcut.parent_path());
        result = persist->Save(shortcut.c_str(), TRUE);
        if (FAILED(result)) throw std::runtime_error("Could not save the Studio shortcut.");
    } catch (...) {
        if (programsFolder != nullptr) CoTaskMemFree(programsFolder);
        if (persist != nullptr) persist->Release();
        if (link != nullptr) link->Release();
        if (SUCCEEDED(initResult)) CoUninitialize();
        throw;
    }
    if (programsFolder != nullptr) CoTaskMemFree(programsFolder);
    if (persist != nullptr) persist->Release();
    if (link != nullptr) link->Release();
    if (SUCCEEDED(initResult)) CoUninitialize();
#else
    (void)launcherExecutable;
    throw std::runtime_error("The Studio shortcut is available only on Windows.");
#endif
}

void CreateBluboxClientShortcuts(const std::filesystem::path &launcherExecutable) {
#ifdef _WIN32
    CreateAppShortcuts(launcherExecutable, L"Blubox", L"Install and launch Blubox");
#else
    (void)launcherExecutable;
#endif
}

void CreateBluboxStudioShortcuts(const std::filesystem::path &launcherExecutable) {
#ifdef _WIN32
    CreateAppShortcuts(launcherExecutable, L"Blubox Studio", L"Install and launch Blubox Studio");
#else
    (void)launcherExecutable;
#endif
}

void RegisterBluboxClientUninstaller(const std::filesystem::path &launcherExecutable,
                                     std::string_view version) {
#ifdef _WIN32
    // Inno Setup owns the Add/Remove Programs entry. Older launchers created a
    // second entry here; remove that legacy entry instead of recreating it.
    (void)launcherExecutable;
    (void)version;
    RemoveUninstaller(L"Blubox");
#else
    (void)launcherExecutable;
    (void)version;
#endif
}

void RegisterBluboxStudioUninstaller(const std::filesystem::path &launcherExecutable,
                                     std::string_view version) {
#ifdef _WIN32
    // Inno Setup owns the Add/Remove Programs entry. Older launchers created a
    // second entry here; remove that legacy entry instead of recreating it.
    (void)launcherExecutable;
    (void)version;
    RemoveUninstaller(L"BluboxStudio");
#else
    (void)launcherExecutable;
    (void)version;
#endif
}

void UninstallBluboxComponent(const std::filesystem::path &installRoot, bool studio) {
#ifdef _WIN32
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(installRoot).lexically_normal();
    if (absoluteRoot.filename() != L"Blubox" || !absoluteRoot.is_absolute()) {
        throw std::runtime_error("The Blubox installation path is not safe to remove.");
    }
    if (studio) {
        RemoveUninstaller(L"BluboxStudio");
        DeleteAppShortcuts(L"Blubox Studio");
        const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, kStudioProtocolRegistryPath);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
            ThrowRegistryError("Removing the Blubox Studio protocol", status);
        }
    } else {
        RemoveUninstaller(L"Blubox");
        DeleteAppShortcuts(L"Blubox");
        UnregisterBluboxProtocolForCurrentUser();
    }
    ScheduleFileCleanup(absoluteRoot, studio);
#else
    (void)installRoot;
    (void)studio;
    throw std::runtime_error("Blubox uninstallation is available only on Windows.");
#endif
}
