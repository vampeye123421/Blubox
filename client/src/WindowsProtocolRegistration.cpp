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

namespace {

constexpr wchar_t kProtocolRegistryPath[] = L"Software\\Classes\\blubox";

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
