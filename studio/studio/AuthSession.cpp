#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <shlobj.h>

#include "AuthSession.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace blubox::studio {
namespace {

constexpr std::string_view kLoginUrl = "https://blubox.runs-on.dev/auth/studio";
constexpr std::string_view kEntropyText = "Blubox Studio session v1";
constexpr std::string_view kProtocolPrefix = "blubox-studio://";
constexpr std::string_view kBase64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const BYTE* data, std::size_t size) {
    std::string result;
    result.reserve(((size + 2) / 3) * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const unsigned int a = data[i];
        const unsigned int b = i + 1 < size ? data[i + 1] : 0;
        const unsigned int c = i + 2 < size ? data[i + 2] : 0;
        const unsigned int block = (a << 16) | (b << 8) | c;
        result.push_back(kBase64Chars[(block >> 18) & 63]);
        result.push_back(kBase64Chars[(block >> 12) & 63]);
        result.push_back(i + 1 < size ? kBase64Chars[(block >> 6) & 63] : '=');
        result.push_back(i + 2 < size ? kBase64Chars[block & 63] : '=');
    }
    return result;
}

std::vector<BYTE> Base64Decode(std::string_view text) {
    if (text.empty() || text.size() % 4 != 0 || text.size() > 32768) {
        throw std::runtime_error("The protected Studio session file is malformed.");
    }
    std::array<signed char, 256> table{};
    table.fill(-1);
    for (std::size_t i = 0; i < kBase64Chars.size(); ++i) {
        table[static_cast<unsigned char>(kBase64Chars[i])] = static_cast<signed char>(i);
    }
    std::vector<BYTE> result;
    result.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        unsigned int block = 0;
        int padding = 0;
        for (int j = 0; j < 4; ++j) {
            const char ch = text[i + static_cast<std::size_t>(j)];
            if (ch == '=') {
                ++padding;
                block <<= 6;
            } else {
                if (padding != 0 || table[static_cast<unsigned char>(ch)] < 0) {
                    throw std::runtime_error("The protected Studio session file is malformed.");
                }
                block = (block << 6) | static_cast<unsigned char>(table[static_cast<unsigned char>(ch)]);
            }
        }
        result.push_back(static_cast<BYTE>((block >> 16) & 0xff));
        if (padding < 2) result.push_back(static_cast<BYTE>((block >> 8) & 0xff));
        if (padding < 1) result.push_back(static_cast<BYTE>(block & 0xff));
    }
    return result;
}

DATA_BLOB EntropyBlob() {
    return {static_cast<DWORD>(kEntropyText.size()),
            reinterpret_cast<BYTE*>(const_cast<char*>(kEntropyText.data()))};
}

HANDLE SessionUpdateEvent() {
    static HANDLE event = CreateEventW(nullptr, FALSE, FALSE,
                                       L"Local\\BluboxStudioSessionUpdated");
    return event;
}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) throw std::runtime_error("The Studio login URL is not valid UTF-8.");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), length);
    return result;
}

int HexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool DecodeQueryValue(std::string_view input, std::string& output) {
    output.clear();
    output.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (ch == '%') {
            if (i + 2 >= input.size()) return false;
            const int high = HexDigit(input[i + 1]);
            const int low = HexDigit(input[i + 2]);
            if (high < 0 || low < 0) return false;
            output.push_back(static_cast<char>((high << 4) | low));
            i += 2;
        } else {
            output.push_back(ch == '+' ? ' ' : ch);
        }
    }
    return output.find_first_of("\r\n\0", 0, 3) == std::string::npos;
}

bool StartsWithInsensitive(std::string_view value, std::string_view prefix) {
    if (value.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        char a = value[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

} // namespace

std::filesystem::path SessionStore::Path() {
    PWSTR localAppData = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData);
    if (FAILED(result) || localAppData == nullptr) throw std::runtime_error("Could not find the current user's Local AppData folder.");
    std::filesystem::path path(localAppData);
    CoTaskMemFree(localAppData);
    path /= L"BluboxStudio";
    std::filesystem::create_directories(path);
    return path / L"session.json";
}

bool SessionStore::Save(std::string_view token, std::string& error) {
    try {
        if (token.empty() || token.size() > 8192) throw std::runtime_error("The website token is missing or too large.");
        DATA_BLOB input{static_cast<DWORD>(token.size()), reinterpret_cast<BYTE*>(const_cast<char*>(token.data()))};
        DATA_BLOB entropy = EntropyBlob();
        DATA_BLOB encrypted{};
        if (!CryptProtectData(&input, L"Blubox Studio session", &entropy, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) {
            throw std::runtime_error("Windows could not protect the Studio session with DPAPI.");
        }
        const std::string protectedToken = Base64Encode(encrypted.pbData, encrypted.cbData);
        SecureZeroMemory(encrypted.pbData, encrypted.cbData);
        LocalFree(encrypted.pbData);

        const auto path = Path();
        auto temporary = path;
        temporary += L".tmp";
        const nlohmann::json data{{"version", 1}, {"protectedToken", protectedToken}};
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) throw std::runtime_error("Could not write the protected Studio session file.");
            const std::string serialized = data.dump();
            file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
            file.flush();
            if (!file) throw std::runtime_error("Could not finish writing the protected Studio session file.");
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());
            throw std::runtime_error("Could not atomically update the protected Studio session file.");
        }
        if (HANDLE event = SessionUpdateEvent(); event != nullptr) SetEvent(event);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

std::optional<std::string> SessionStore::Load(std::string& error) {
    try {
        const auto path = Path();
        if (!std::filesystem::exists(path)) { error.clear(); return std::nullopt; }
        const auto size = std::filesystem::file_size(path);
        if (size == 0 || size > 32768) throw std::runtime_error("The protected Studio session file is invalid.");
        std::ifstream file(path, std::ios::binary);
        const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto data = nlohmann::json::parse(contents);
        if (data.value("version", 0) != 1) throw std::runtime_error("Unsupported protected Studio session file version.");
        const std::vector<BYTE> bytes = Base64Decode(data.at("protectedToken").get<std::string>());
        DATA_BLOB encrypted{static_cast<DWORD>(bytes.size()), const_cast<BYTE*>(bytes.data())};
        DATA_BLOB entropy = EntropyBlob();
        DATA_BLOB decrypted{};
        if (!CryptUnprotectData(&encrypted, nullptr, &entropy, nullptr, nullptr,
                                CRYPTPROTECT_UI_FORBIDDEN, &decrypted)) {
            throw std::runtime_error("The saved Studio session cannot be decrypted for this Windows user.");
        }
        std::string token(reinterpret_cast<char*>(decrypted.pbData), decrypted.cbData);
        SecureZeroMemory(decrypted.pbData, decrypted.cbData);
        LocalFree(decrypted.pbData);
        if (token.empty() || token.size() > 8192) throw std::runtime_error("The saved Studio token is invalid.");
        error.clear();
        return token;
    } catch (const std::exception& exception) {
        error = exception.what();
        return std::nullopt;
    }
}

bool SessionStore::Clear(std::string& error) {
    try {
        const auto path = Path();
        if (!DeleteFileW(path.c_str())) {
            const DWORD code = GetLastError();
            if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
                throw std::runtime_error("Could not remove the saved Studio session file.");
            }
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

std::optional<std::filesystem::file_time_type> SessionStore::ModifiedTime() {
    try {
        const auto path = Path();
        if (!std::filesystem::exists(path)) return std::nullopt;
        return std::filesystem::last_write_time(path);
    } catch (...) {
        return std::nullopt;
    }
}

bool SessionStore::ConsumeUpdateSignal() {
    HANDLE event = SessionUpdateEvent();
    return event != nullptr && WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
}

bool ParseStudioAuthUrl(std::string_view argument, std::string& token, std::string& error) {
    token.clear();
    error.clear();
    if (!StartsWithInsensitive(argument, kProtocolPrefix)) return false;
    const std::size_t queryAt = argument.find('?');
    // Windows commonly canonicalizes a custom URI with an empty path by
    // inserting a slash before the query, so both auth?token=... and
    // auth/?token=... are valid activations of this handler.
    const std::string_view authorityAndPath = queryAt == std::string_view::npos
        ? std::string_view{} : argument.substr(kProtocolPrefix.size(), queryAt - kProtocolPrefix.size());
    if (queryAt == std::string_view::npos ||
        (authorityAndPath != "auth" && authorityAndPath != "auth/")) {
        error = "The Blubox Studio authentication link is malformed.";
        return false;
    }
    const std::string_view query = argument.substr(queryAt + 1);
    bool foundToken = false;
    for (std::size_t start = 0; start <= query.size();) {
        const std::size_t end = query.find('&', start);
        const std::string_view pair = query.substr(start, end == std::string_view::npos ? query.size() - start : end - start);
        const std::size_t equals = pair.find('=');
        if (pair.substr(0, equals) == "token") {
            if (foundToken || equals == std::string_view::npos ||
                !DecodeQueryValue(pair.substr(equals + 1), token) || token.empty() || token.size() > 8192) {
                error = "The Blubox Studio authentication token is invalid.";
                token.clear();
                return false;
            }
            foundToken = true;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    if (!foundToken) {
        error = "The Blubox Studio authentication link did not include a token.";
        return false;
    }
    return true;
}

bool CreateStudioHandoffId(std::string& handoffId, std::string& error) {
    std::array<unsigned char, 32> randomBytes{};
    const NTSTATUS status = BCryptGenRandom(nullptr, randomBytes.data(),
        static_cast<ULONG>(randomBytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        error = "Windows could not create a secure Studio sign-in request.";
        return false;
    }
    constexpr char hex[] = "0123456789abcdef";
    handoffId.clear();
    handoffId.reserve(randomBytes.size() * 2);
    for (const unsigned char byte : randomBytes) {
        handoffId.push_back(hex[(byte >> 4) & 0x0f]);
        handoffId.push_back(hex[byte & 0x0f]);
    }
    SecureZeroMemory(randomBytes.data(), randomBytes.size());
    error.clear();
    return true;
}

bool OpenStudioLoginPage(std::string_view handoffId, std::string& error) {
    const char* configured = std::getenv("BLUBOX_STUDIO_LOGIN_URL");
    const std::string_view loginUrl = configured != nullptr && *configured != '\0'
        ? std::string_view(configured) : kLoginUrl;
    std::wstring url;
    try {
        if (handoffId.size() != 64 ||
            !std::all_of(handoffId.begin(), handoffId.end(), [](char ch) {
                return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            })) {
            throw std::runtime_error("The Studio sign-in request is invalid.");
        }
        const std::string loginUrlWithHandoff = std::string(loginUrl) + "#studio_handoff=" + std::string(handoffId);
        url = Utf8ToWide(loginUrlWithHandoff);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        error = "Windows could not open the Blubox sign-in page in your browser.";
        return false;
    }
    error.clear();
    return true;
}

StudioInstanceLock::StudioInstanceLock() {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\BluboxStudioSingleton");
    if (mutex == nullptr) throw std::runtime_error("Could not create the Blubox Studio single-instance lock.");
    handle_ = mutex;
    primary_ = GetLastError() != ERROR_ALREADY_EXISTS;
}

StudioInstanceLock::~StudioInstanceLock() {
    if (handle_ != nullptr) {
        if (primary_) ReleaseMutex(static_cast<HANDLE>(handle_));
        CloseHandle(static_cast<HANDLE>(handle_));
    }
}

} // namespace blubox::studio
