#include "PresenceReporter.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

namespace {

void SecureErase(std::string &value) noexcept {
    volatile char *bytes = value.empty() ? nullptr : value.data();
    for (std::size_t i = 0; i < value.size(); ++i) bytes[i] = '\0';
    value.clear();
}

bool IsPresenceToken(std::string_view token) {
    return token.size() == 64 && std::all_of(token.begin(), token.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

bool PostPresence(std::wstring_view route, std::string_view token) noexcept {
#if defined(_WIN32)
    HINTERNET session = WinHttpOpen(L"BluboxClient/Presence", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 2500, 2500, 2500, 2500);
    HINTERNET connection = WinHttpConnect(session, L"blubox.runs-on.dev",
                                          INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection
        ? WinHttpOpenRequest(connection, L"POST", std::wstring(route).c_str(), nullptr,
                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             WINHTTP_FLAG_SECURE)
        : nullptr;
    bool ok = false;
    if (request) {
        const std::wstring headers = L"Content-Type: application/json\r\n";
        const std::string body = std::string("{\"token\":\"") + std::string(token) + "\"}";
        if (WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()),
                               const_cast<char *>(body.data()), static_cast<DWORD>(body.size()),
                               static_cast<DWORD>(body.size()), 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0;
            DWORD statusSize = sizeof(status);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                    WINHTTP_NO_HEADER_INDEX)) {
                ok = status >= 200 && status < 300;
            }
        }
        WinHttpCloseHandle(request);
    }
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
#else
    (void)route;
    (void)token;
    return false;
#endif
}

} // namespace

PresenceReporter::~PresenceReporter() { Stop(); }

void PresenceReporter::Start(std::string token) {
    Stop();
    if (!IsPresenceToken(token)) {
        SecureErase(token);
        return;
    }
    {
        std::lock_guard lock(mutex_);
        token_ = std::move(token);
    }
    worker_ = std::jthread([this](std::stop_token stopToken) { Run(stopToken); });
}

void PresenceReporter::Stop() noexcept {
    if (worker_.joinable()) {
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
    }
    std::lock_guard lock(mutex_);
    SecureErase(token_);
}

void PresenceReporter::Run(std::stop_token stopToken) {
    constexpr std::wstring_view heartbeatRoute = L"/presence/heartbeat";
    constexpr std::wstring_view endRoute = L"/presence/end";
    while (!stopToken.stop_requested()) {
        std::string token;
        {
            std::lock_guard lock(mutex_);
            token = token_;
        }
        if (!token.empty() && !PostPresence(heartbeatRoute, token)) {
            // Heartbeats are best-effort. The website marks a missing client
            // offline after 90 seconds without letting network trouble block play.
        }
        SecureErase(token);

        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, stopToken, std::chrono::seconds(30), [] { return false; });
    }

    std::string token;
    {
        std::lock_guard lock(mutex_);
        token = token_;
    }
    if (!token.empty()) PostPresence(endRoute, token);
    SecureErase(token);
}
