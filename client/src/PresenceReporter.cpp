#include "PresenceReporter.hpp"

#include "HttpClient.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <string_view>

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

bool PostPresence(std::string_view route, std::string_view token) noexcept {
    try {
        const std::string body = std::string("{\"token\":\"") + std::string(token) + "\"}";
        blubox::platform::HttpRequest request;
        request.url = "https://blubox.runs-on.dev" + std::string(route);
        request.method = "POST";
        request.headers = {"Content-Type: application/json"};
        request.body = body;
        request.timeoutMs = 2500;
        request.maxResponseBytes = 1024;
        const auto response = blubox::platform::PerformHttpRequest(request);
        return response.status >= 200 && response.status < 300;
    } catch (...) {
        return false;
    }
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
    constexpr std::string_view heartbeatRoute = "/presence/heartbeat";
    constexpr std::string_view endRoute = "/presence/end";
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
        wake_.wait_for(lock, stopToken, std::chrono::seconds(5), [] { return false; });
    }

    std::string token;
    {
        std::lock_guard lock(mutex_);
        token = token_;
    }
    if (!token.empty()) PostPresence(endRoute, token);
    SecureErase(token);
}
