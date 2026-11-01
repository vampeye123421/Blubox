#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace blubox::platform {

struct HttpResponse {
    long status = 0;
    std::string body;
};

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::vector<std::string> headers;
    std::string_view body;
    long timeoutMs = 30000;
    std::size_t maxResponseBytes = 64u * 1024u * 1024u;
    std::function<void(const char*, std::size_t)> onChunk;
    std::function<void(std::uint64_t, std::uint64_t)> onProgress;
};

HttpResponse PerformHttpRequest(const HttpRequest& request);

} // namespace blubox::platform
