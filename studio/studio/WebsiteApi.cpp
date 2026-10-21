#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include "WebsiteApi.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <vector>

namespace blubox::studio {
namespace {

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("API URL or token is not valid UTF-8.");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count);
    return result;
}

struct InternetHandle {
    HINTERNET value = nullptr;
    explicit InternetHandle(HINTERNET handle = nullptr) : value(handle) {}
    ~InternetHandle() { if (value != nullptr) WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};

struct HttpResult {
    DWORD status = 0;
    std::string body;
};

bool IsLoopbackHost(std::wstring_view host);

HttpResult Request(std::string_view baseUrl, std::wstring_view method,
                   std::wstring_view route, std::string_view accessToken,
                   std::string_view body, bool allowAnonymous = false) {
    if (baseUrl.size() > 2048 || accessToken.size() > 8192 || body.size() > 64 * 1024 * 1024) {
        throw std::runtime_error("Website API request exceeds the supported size.");
    }
    if (accessToken.empty() && !allowAnonymous) {
        throw std::runtime_error("Sign in through the Blubox website before using this Studio API.");
    }
    if (accessToken.find_first_of("\r\n") != std::string_view::npos) {
        throw std::runtime_error("The website session token contains invalid header characters.");
    }

    const std::wstring wideUrl = Utf8ToWide(baseUrl);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts)) {
        throw std::runtime_error("Blubox Studio API URL is invalid.");
    }
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    const bool localHttp = parts.nScheme == INTERNET_SCHEME_HTTP && IsLoopbackHost(host);
    if (!secure && !localHttp) {
        throw std::runtime_error("Blubox Studio requires HTTPS, except for HTTP loopback development servers.");
    }
    std::wstring basePath(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (basePath.empty() || basePath.back() != L'/') basePath.push_back(L'/');
    std::wstring requestPath = basePath + std::wstring(route);
    if (parts.dwExtraInfoLength > 0) {
        requestPath.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }

    InternetHandle session(WinHttpOpen(L"BluboxStudio/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value) throw std::runtime_error("Could not initialize the Windows HTTPS client.");
    WinHttpSetTimeouts(session.value, 10000, 10000, 15000, 30000);
    InternetHandle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
    if (!connection.value) throw std::runtime_error("Could not connect to the Blubox website API.");
    InternetHandle request(WinHttpOpenRequest(connection.value, std::wstring(method).c_str(),
                                               requestPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request.value) throw std::runtime_error("Could not create the website API request.");

    const wchar_t* headers = L"Content-Type: application/json; charset=utf-8\r\nAccept: application/json\r\n";
    if (!WinHttpAddRequestHeaders(request.value, headers, static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD)) {
        throw std::runtime_error("Could not set website API request headers.");
    }
    if (!accessToken.empty()) {
        const std::wstring authorization = L"Authorization: Bearer " + Utf8ToWide(accessToken) + L"\r\n";
        if (!WinHttpAddRequestHeaders(request.value, authorization.c_str(), static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD)) {
            throw std::runtime_error("Could not set website API authorization header.");
        }
    }
    const DWORD bodySize = static_cast<DWORD>(body.size());
    const void* bodyData = body.empty() ? WINHTTP_NO_REQUEST_DATA : body.data();
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            const_cast<void*>(bodyData), bodySize, bodySize, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        throw std::runtime_error("The website API request failed. Check the network and API URL.");
    }

    HttpResult result;
    DWORD statusSize = sizeof(result.status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &result.status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        throw std::runtime_error("The website API returned no HTTP status.");
    }
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) {
            throw std::runtime_error("Could not read the website API response.");
        }
        if (available == 0) break;
        if (result.body.size() + available > 64 * 1024 * 1024) {
            throw std::runtime_error("The website API response is too large.");
        }
        const std::size_t offset = result.body.size();
        result.body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.value, result.body.data() + offset, available, &read)) {
            throw std::runtime_error("Could not read the website API response.");
        }
        result.body.resize(offset + read);
    }
    return result;
}

bool IsSafeWorldId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (const unsigned char ch : id) {
        if (!(std::isalnum(ch) || ch == '-' || ch == '_')) return false;
    }
    return true;
}

bool IsLoopbackHost(std::wstring_view host) {
    std::wstring normalized(host);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return normalized == L"localhost" || normalized == L"127.0.0.1" ||
           normalized == L"::1" || normalized == L"[::1]";
}

} // namespace

WebsiteApi::WebsiteApi() {
    const char* configured = std::getenv("BLUBOX_STUDIO_API_BASE_URL");
    baseUrl_ = configured != nullptr && *configured != '\0'
        ? configured : "https://blubox-mailer.onrender.com";
}

bool WebsiteApi::PublishMap(const MapDocument& document, std::string_view accessToken,
                            std::string& error) const {
    try {
        const HttpResult result = Request(baseUrl_, L"POST", L"api/studio/maps",
                                          accessToken, document.ToJson().dump());
        if (result.status < 200 || result.status >= 300) {
            if (result.status == 404) {
                throw std::runtime_error("Studio API route is missing (HTTP 404). Redeploy the Blubox website mailer service to enable Studio publishing.");
            }
            std::string detail;
            try {
                const auto payload = nlohmann::json::parse(result.body);
                if (payload.contains("error") && payload.at("error").is_string()) detail = payload.at("error").get<std::string>();
            } catch (...) {}
            throw std::runtime_error("Map save was rejected by the website (HTTP " +
                                     std::to_string(result.status) + ")." + (detail.empty() ? std::string{} : " " + detail));
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool WebsiteApi::PublishGame(MapDocument& document, std::string_view title,
                             std::string_view description, std::string_view accessToken,
                             std::string& resultMessage, std::string& error) const {
    try {
        const std::string cleanTitle(title);
        const std::string cleanDescription(description);
        if (cleanTitle.empty() || cleanTitle.size() > 64) {
            throw std::runtime_error("Game name must be between 1 and 64 characters.");
        }
        if (cleanDescription.size() > 1000) {
            throw std::runtime_error("Description must be 1000 characters or fewer.");
        }
        nlohmann::json payload = {
            {"title", cleanTitle},
            {"description", cleanDescription},
            {"map", document.ToJson()},
        };
        const HttpResult response = Request(baseUrl_, L"POST", L"api/studio/publish",
                                            accessToken, payload.dump());
        if (response.status < 200 || response.status >= 300) {
            if (response.status == 404) {
                throw std::runtime_error("Studio publish endpoint is missing (HTTP 404). Redeploy the Blubox website mailer service, then try again.");
            }
            std::string detail;
            try {
                const auto body = nlohmann::json::parse(response.body);
                if (body.contains("error") && body.at("error").is_string()) detail = body.at("error").get<std::string>();
            } catch (...) {}
            throw std::runtime_error("Website rejected this game (HTTP " + std::to_string(response.status) + ")." +
                                     (detail.empty() ? std::string{} : " " + detail));
        }
        const auto body = nlohmann::json::parse(response.body);
        const std::string publishedWorldId = body.value("worldId", document.worldId);
        if (!publishedWorldId.empty()) document.worldId = publishedWorldId;
        resultMessage = body.value("status", std::string("pending")) == "published"
            ? "Game published to the website."
            : "Game submitted to the website for review.";
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        resultMessage.clear();
        error = exception.what();
        return false;
    }
}

bool WebsiteApi::DownloadMap(std::string_view worldId, std::string_view accessToken,
                             MapDocument& document, std::string& error) const {
    try {
        if (!IsSafeWorldId(worldId)) throw std::runtime_error("Invalid Blubox world id.");
        const std::wstring route = L"api/studio/maps/" + Utf8ToWide(worldId);
        const HttpResult result = Request(baseUrl_, L"GET", route, accessToken, {});
        if (result.status < 200 || result.status >= 300) {
            throw std::runtime_error("Map download was rejected by the website (HTTP " +
                                     std::to_string(result.status) + ").");
        }
        const nlohmann::json payload = nlohmann::json::parse(result.body);
        document = MapDocument::FromJson(payload.contains("map") ? payload.at("map") : payload);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool WebsiteApi::ValidateSession(std::string_view accessToken, std::string& displayName,
                                 std::string& error) const {
    try {
        const HttpResult result = Request(baseUrl_, L"GET", L"api/studio/session", accessToken, {});
        if (result.status < 200 || result.status >= 300) {
            std::string detail;
            try {
                const auto payload = nlohmann::json::parse(result.body);
                if (payload.contains("error") && payload.at("error").is_string()) {
                    detail = payload.at("error").get<std::string>();
                }
            } catch (...) {}
            throw std::runtime_error("The Blubox website rejected this session (HTTP " +
                                     std::to_string(result.status) + ")." +
                                     (detail.empty() ? std::string{} : " " + detail) +
                                     " Please sign in again.");
        }
        const nlohmann::json payload = nlohmann::json::parse(result.body);
        const nlohmann::json user = payload.contains("user") && payload.at("user").is_object()
            ? payload.at("user") : payload;
        const std::string userId = user.value("id", std::string{});
        if (userId.empty()) throw std::runtime_error("The website session response did not include an authenticated user.");
        displayName = user.value("displayName", user.value("display_name", user.value("name", user.value("email", std::string("Blubox user")))));
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        displayName.clear();
        error = exception.what();
        return false;
    }
}

bool WebsiteApi::PollStudioHandoff(std::string_view handoffId, std::string& accessToken,
                                   std::string& error) const {
    accessToken.clear();
    try {
        if (handoffId.size() != 64 || !std::all_of(handoffId.begin(), handoffId.end(), [](char ch) {
                return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            })) {
            throw std::runtime_error("The Studio sign-in request is invalid.");
        }

        const nlohmann::json body{{"handoffId", handoffId}};
        HttpResult result = Request(baseUrl_, L"POST", L"api/studio/handoff/redeem", {}, body.dump(), true);
        if (result.status == 202) {
            error.clear();
            return true;
        }
        if (result.status < 200 || result.status >= 300) {
            std::string detail;
            try {
                const auto payload = nlohmann::json::parse(result.body);
                if (payload.contains("error") && payload.at("error").is_string()) {
                    detail = payload.at("error").get<std::string>();
                }
            } catch (...) {}
            throw std::runtime_error("The website could not complete Studio sign-in (HTTP " +
                std::to_string(result.status) + ")." + (detail.empty() ? std::string{} : " " + detail));
        }

        const nlohmann::json payload = nlohmann::json::parse(result.body);
        accessToken = payload.value("accessToken", std::string{});
        std::fill(result.body.begin(), result.body.end(), '\0');
        if (accessToken.empty() || accessToken.size() > 8192) {
            std::fill(accessToken.begin(), accessToken.end(), '\0');
            accessToken.clear();
            throw std::runtime_error("The website returned an invalid Studio session.");
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace blubox::studio
