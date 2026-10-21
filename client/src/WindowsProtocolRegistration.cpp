#include "WindowsProtocolRegistration.hpp"

#include <stdexcept>
#include <string>
#include <system_error>

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
    throw std::runtime_error(std::string(operation) + " non riuscita: " +
                             std::system_category().message(static_cast<int>(status)));
}

void SetStringValue(HKEY key, const wchar_t *valueName, const std::wstring &value) {
    const DWORD byteCount = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(
        key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()), byteCount);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Scrittura del registro", status);
}

std::wstring GetCurrentExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0) {
        ThrowRegistryError("Lettura del percorso dell'eseguibile", GetLastError());
    }
    if (length >= path.size()) {
        throw std::runtime_error("Il percorso dell'eseguibile e' troppo lungo per registrare blubox://.");
    }
    path.resize(length);
    return path;
}

} // namespace
#endif

void RegisterBluboxProtocolForCurrentUser() {
#ifdef _WIN32
    const std::wstring executablePath = GetCurrentExecutablePath();

    RegistryKey protocolKey;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, kProtocolRegistryPath, 0, nullptr,
                                     REG_OPTION_NON_VOLATILE,
                                     KEY_SET_VALUE | KEY_CREATE_SUB_KEY, nullptr,
                                     protocolKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creazione della chiave blubox", status);

    SetStringValue(protocolKey.Get(), nullptr, L"URL:Blubox Protocol");
    SetStringValue(protocolKey.Get(), L"URL Protocol", L"");

    RegistryKey iconKey;
    status = RegCreateKeyExW(protocolKey.Get(), L"DefaultIcon", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                             iconKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creazione dell'icona blubox", status);
    std::wstring icon = L"\"";
    icon += executablePath;
    icon += L"\",0";
    SetStringValue(iconKey.Get(), nullptr, icon);

    RegistryKey commandKey;
    status = RegCreateKeyExW(protocolKey.Get(), L"shell\\open\\command", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                             commandKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creazione del comando blubox", status);

    std::wstring command = L"\"";
    command += executablePath;
    command += L"\" \"%1\"";
    SetStringValue(commandKey.Get(), nullptr, command);
#else
    throw std::runtime_error("La registrazione del protocollo blubox:// e' disponibile solo su Windows.");
#endif
}

void UnregisterBluboxProtocolForCurrentUser() {
#ifdef _WIN32
    LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, kProtocolRegistryPath);
    if (status == ERROR_FILE_NOT_FOUND) return;
    if (status != ERROR_SUCCESS) ThrowRegistryError("Rimozione della registrazione blubox", status);

    status = RegDeleteKeyW(HKEY_CURRENT_USER, kProtocolRegistryPath);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        ThrowRegistryError("Rimozione della chiave blubox", status);
    }
#else
    throw std::runtime_error("La rimozione del protocollo blubox:// e' disponibile solo su Windows.");
#endif
}

void RegisterBluboxStudioProtocolForCurrentUser(const std::filesystem::path &studioExecutable) {
#ifdef _WIN32
    const std::wstring executablePath = studioExecutable.wstring();
    RegistryKey protocolKey;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, kStudioProtocolRegistryPath, 0, nullptr,
                                     REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_CREATE_SUB_KEY,
                                     nullptr, protocolKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creazione della chiave blubox-studio", status);
    SetStringValue(protocolKey.Get(), nullptr, L"URL:Blubox Studio Protocol");
    SetStringValue(protocolKey.Get(), L"URL Protocol", L"");

    RegistryKey iconKey;
    status = RegCreateKeyExW(protocolKey.Get(), L"DefaultIcon", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                             iconKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creazione dell'icona blubox-studio", status);
    SetStringValue(iconKey.Get(), nullptr, L"\"" + executablePath + L"\",0");

    RegistryKey commandKey;
    status = RegCreateKeyExW(protocolKey.Get(), L"shell\\open\\command", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                             commandKey.Put(), nullptr);
    if (status != ERROR_SUCCESS) ThrowRegistryError("Creazione del comando blubox-studio", status);
    SetStringValue(commandKey.Get(), nullptr, L"\"" + executablePath + L"\" \"%1\"");
#else
    (void)studioExecutable;
    throw std::runtime_error("La registrazione blubox-studio:// e' disponibile solo su Windows.");
#endif
}

void CreateBluboxStudioStartMenuShortcut(const std::filesystem::path &launcherExecutable) {
#ifdef _WIN32
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
        throw std::runtime_error("Inizializzazione COM per il collegamento Studio non riuscita.");
    }
    IShellLinkW *link = nullptr;
    IPersistFile *persist = nullptr;
    PWSTR programsFolder = nullptr;
    try {
        HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&link));
        if (FAILED(result)) throw std::runtime_error("Creazione del collegamento Studio non riuscita.");
        const std::wstring path = launcherExecutable.wstring();
        if (FAILED(link->SetPath(path.c_str())) || FAILED(link->SetArguments(L"--studio")) ||
            FAILED(link->SetWorkingDirectory(launcherExecutable.parent_path().wstring().c_str())) ||
            FAILED(link->SetDescription(L"Install and launch Blubox Studio"))) {
            throw std::runtime_error("Configurazione del collegamento Studio non riuscita.");
        }
        result = link->QueryInterface(IID_PPV_ARGS(&persist));
        if (FAILED(result)) throw std::runtime_error("Salvataggio del collegamento Studio non disponibile.");
        result = SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_CREATE, nullptr, &programsFolder);
        if (FAILED(result)) throw std::runtime_error("Percorso Start Menu non disponibile.");
        const std::filesystem::path shortcut = std::filesystem::path(programsFolder) / L"Blubox Studio.lnk";
        result = persist->Save(shortcut.c_str(), TRUE);
        if (FAILED(result)) throw std::runtime_error("Salvataggio del collegamento Studio non riuscito.");
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
    throw std::runtime_error("Il collegamento Studio e' disponibile solo su Windows.");
#endif
}
