#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOMINMAX
#include "WebsiteApi.hpp"
#include "HttpClient.hpp"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <curl/curl.h>
#include <limits>
#include <stdexcept>

namespace blubox::studio {
namespace {

struct HttpResult {
    long status = 0;
    std::string body;
};

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool IsLoopbackHost(std::string_view host) {
    return host == "localhost" || host == "127.0.0.1" ||
           host == "::1" || host == "[::1]";
}

HttpResult Request(std::string_view baseUrl, std::string_view method,
                   std::string_view route, std::string_view accessToken,
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

    CURLU* parsed = curl_url();
    if (parsed == nullptr) throw std::runtime_error("Could not validate the Blubox Studio API URL.");
    const std::string base(baseUrl);
    if (curl_url_set(parsed, CURLUPART_URL, base.c_str(), 0) != CURLUE_OK) {
        curl_url_cleanup(parsed);
        throw std::runtime_error("Blubox Studio API URL is invalid.");
    }
    char* schemeValue = nullptr;
    char* hostValue = nullptr;
    const CURLUcode schemeResult = curl_url_get(parsed, CURLUPART_SCHEME, &schemeValue, 0);
    const CURLUcode hostResult = curl_url_get(parsed, CURLUPART_HOST, &hostValue, 0);
    const std::string scheme = schemeResult == CURLUE_OK ? Lowercase(schemeValue) : std::string{};
    const std::string host = hostResult == CURLUE_OK ? Lowercase(hostValue) : std::string{};
    curl_free(schemeValue);
    curl_free(hostValue);
    curl_url_cleanup(parsed);
    const bool secure = scheme == "https";
    const bool localHttp = scheme == "http" && IsLoopbackHost(host);
    if (host.empty() || (!secure && !localHttp)) {
        throw std::runtime_error("Blubox Studio requires HTTPS, except for HTTP loopback development servers.");
    }
    std::string url(baseUrl);
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += '/';
    url += route;
    blubox::platform::HttpRequest request;
    request.url = std::move(url);
    request.method = std::string(method);
    request.headers = {"Content-Type: application/json; charset=utf-8", "Accept: application/json"};
    if (!accessToken.empty()) request.headers.push_back("Authorization: Bearer " + std::string(accessToken));
    request.body = body;
    request.timeoutMs = 30000;
    const auto response = blubox::platform::PerformHttpRequest(request);
    return {response.status, response.body};
}

bool IsSafeWorldId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (const unsigned char ch : id) {
        if (!(std::isalnum(ch) || ch == '-' || ch == '_')) return false;
    }
    return true;
}

} // namespace

WebsiteApi::WebsiteApi() {
    const char* configured = std::getenv("BLUBOX_STUDIO_API_BASE_URL");
    baseUrl_ = configured != nullptr && *configured != '\0'
        ? configured : "https://blubox.runs-on.dev";
}

bool WebsiteApi::PublishMap(const MapDocument& document, std::string_view accessToken,
                            std::string& error) const {
    try {
        const HttpResult result = Request(baseUrl_, "POST", "api/studio/maps",
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
                             std::string& resultMessage, std::string& error,
                             std::string_view mode, std::string_view targetWorldId) const {
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
            {"mode", mode == "update" ? "update" : "create"},
            {"map", document.ToJson()},
        };
        if (mode == "update") {
            if (!IsSafeWorldId(targetWorldId)) throw std::runtime_error("Choose a valid game to update.");
            payload["targetWorldId"] = targetWorldId;
        }
        const HttpResult response = Request(baseUrl_, "POST", "api/studio/publish",
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
        const bool updating = mode == "update";
        resultMessage = body.value("status", std::string("pending")) == "published"
            ? (updating ? "Game updated on the website." : "New game published to the website.")
            : (updating ? "Game update submitted for review." : "New game submitted to the website for review.");
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        resultMessage.clear();
        error = exception.what();
        return false;
    }
}

bool WebsiteApi::ListOwnedGames(std::string_view accessToken,
                                std::vector<OwnedPublishedGame>& games,
                                std::string& error) const {
    try {
        const HttpResult response = Request(baseUrl_, "GET", "api/studio/games",
                                            accessToken, {});
        if (response.status < 200 || response.status >= 300) {
            std::string detail;
            try {
                const auto body = nlohmann::json::parse(response.body);
                if (body.contains("error") && body.at("error").is_string()) detail = body.at("error").get<std::string>();
            } catch (...) {}
            throw std::runtime_error("Could not load your published games (HTTP " +
                std::to_string(response.status) + ")." + (detail.empty() ? std::string{} : " " + detail));
        }
        const auto body = nlohmann::json::parse(response.body);
        games.clear();
        if (body.contains("games") && body.at("games").is_array()) {
            for (const auto& row : body.at("games")) {
                if (!row.is_object()) continue;
                OwnedPublishedGame game;
                game.worldId = row.value("worldId", std::string{});
                game.title = row.value("title", std::string{});
                game.slug = row.value("slug", std::string{});
                game.status = row.value("status", std::string{});
                if (IsSafeWorldId(game.worldId) && !game.title.empty()) games.push_back(std::move(game));
            }
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        games.clear();
        error = exception.what();
        return false;
    }
}

bool WebsiteApi::DownloadMap(std::string_view worldId, std::string_view accessToken,
                             MapDocument& document, std::string& error) const {
    try {
        if (!IsSafeWorldId(worldId)) throw std::runtime_error("Invalid Blubox world id.");
        const std::string route = "api/studio/maps/" + std::string(worldId);
        const HttpResult result = Request(baseUrl_, "GET", route, accessToken, {});
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
        const HttpResult result = Request(baseUrl_, "GET", "api/studio/session", accessToken, {});
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
        HttpResult result = Request(baseUrl_, "POST", "api/studio/handoff/redeem", {}, body.dump(), true);
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
