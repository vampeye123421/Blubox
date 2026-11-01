#include "HttpClient.hpp"

#include <curl/curl.h>

#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>

namespace blubox::platform {
namespace {

struct CurlGlobal {
    CurlGlobal() {
        const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (result != CURLE_OK) {
            throw std::runtime_error(std::string("Could not initialize HTTPS: ") +
                                     curl_easy_strerror(result));
        }
    }

    ~CurlGlobal() { curl_global_cleanup(); }
};

CurlGlobal& Global() {
    static CurlGlobal global;
    return global;
}

struct TransferState {
    const HttpRequest* request = nullptr;
    HttpResponse response;
    std::exception_ptr callbackError;
};

std::size_t ReceiveData(char* data, std::size_t size, std::size_t count, void* opaque) {
    auto& state = *static_cast<TransferState*>(opaque);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) return 0;
    const std::size_t bytes = size * count;
    try {
        if (state.request->onChunk) {
            state.request->onChunk(data, bytes);
        } else {
            if (bytes > state.request->maxResponseBytes -
                            std::min(state.response.body.size(), state.request->maxResponseBytes)) {
                throw std::runtime_error("The HTTP response exceeds the supported size.");
            }
            state.response.body.append(data, bytes);
        }
        return bytes;
    } catch (...) {
        state.callbackError = std::current_exception();
        return 0;
    }
}

int ReportProgress(void* opaque, curl_off_t total, curl_off_t received,
                   curl_off_t, curl_off_t) {
    auto& state = *static_cast<TransferState*>(opaque);
    if (!state.request->onProgress) return 0;
    try {
        state.request->onProgress(
            received > 0 ? static_cast<std::uint64_t>(received) : 0,
            total > 0 ? static_cast<std::uint64_t>(total) : 0);
        return 0;
    } catch (...) {
        state.callbackError = std::current_exception();
        return 1;
    }
}

} // namespace

HttpResponse PerformHttpRequest(const HttpRequest& request) {
    if (request.url.empty() || request.url.size() > 8192 ||
        (request.method != "GET" && request.method != "POST" && request.method != "PUT" &&
         request.method != "DELETE" && request.method != "PATCH")) {
        throw std::runtime_error("The HTTP request is invalid.");
    }
    if (request.timeoutMs <= 0 ||
        request.body.size() > static_cast<std::size_t>(std::numeric_limits<curl_off_t>::max())) {
        throw std::runtime_error("The HTTP request exceeds the supported limits.");
    }
    (void)Global();

    using EasyHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    EasyHandle handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) throw std::runtime_error("Could not create an HTTPS request.");

    TransferState state{&request};
    curl_slist* rawHeaders = nullptr;
    for (const auto& header : request.headers) {
        rawHeaders = curl_slist_append(rawHeaders, header.c_str());
        if (!rawHeaders) throw std::runtime_error("Could not allocate HTTP request headers.");
    }
    using HeaderList = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;
    HeaderList headers(rawHeaders, &curl_slist_free_all);

    curl_easy_setopt(handle.get(), CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, request.timeoutMs);
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "Blubox/1.0");
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, ReceiveData);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFOFUNCTION, ReportProgress);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, request.onProgress ? 0L : 1L);
    if (headers) curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
    if (request.method != "GET") {
        curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST, request.method.c_str());
        if (!request.body.empty()) {
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, request.body.data());
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                             static_cast<curl_off_t>(request.body.size()));
        }
    }

    const CURLcode result = curl_easy_perform(handle.get());
    if (state.callbackError) std::rethrow_exception(state.callbackError);
    if (result != CURLE_OK) {
        throw std::runtime_error(std::string("The HTTPS request failed: ") +
                                 curl_easy_strerror(result));
    }
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &state.response.status);
    return std::move(state.response);
}

} // namespace blubox::platform
