#include "ClientSession.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void SecureErase(std::string &value) noexcept {
    volatile char *bytes = value.empty() ? nullptr : value.data();
    for (std::size_t i = 0; i < value.size(); ++i) bytes[i] = '\0';
    value.clear();
}

void SecureErase(std::vector<char> &value) noexcept {
    volatile char *bytes = value.empty() ? nullptr : value.data();
    for (std::size_t i = 0; i < value.size(); ++i) bytes[i] = '\0';
    value.clear();
}

bool IsTokenParameter(const std::string &key) {
    return key == "token" || key == "session_token" || key == "sessiontoken" ||
           key == "presencetoken" || key == "presence_token";
}

std::string DecodeBase64Url(std::string_view input) {
    std::string output;
    output.reserve((input.size() * 3) / 4);
    unsigned int buffer = 0;
    int bits = 0;
    for (const char character : input) {
        unsigned int value = 0;
        if (character >= 'A' && character <= 'Z') value = static_cast<unsigned int>(character - 'A');
        else if (character >= 'a' && character <= 'z') value = static_cast<unsigned int>(character - 'a' + 26);
        else if (character >= '0' && character <= '9') value = static_cast<unsigned int>(character - '0' + 52);
        else if (character == '-' || character == '+') value = 62;
        else if (character == '_' || character == '/') value = 63;
        else if (character == '=') break;
        else return {};

        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>((buffer >> bits) & 0xffu));
        }
    }
    return output;
}

std::string ReadDisplayName(std::string_view token) {
    const std::size_t firstDot = token.find('.');
    if (firstDot == std::string_view::npos) return {};
    const std::size_t secondDot = token.find('.', firstDot + 1);
    if (secondDot == std::string_view::npos || secondDot - firstDot > 8192) return {};

    try {
        const std::string payload = DecodeBase64Url(token.substr(firstDot + 1, secondDot - firstDot - 1));
        const auto claims = nlohmann::json::parse(payload);
        if (!claims.is_object() || !claims.contains("name") || !claims["name"].is_string()) return {};
        std::string name = claims["name"].get<std::string>();
        if (name.empty() || name.size() > 64) return {};
        for (char &character : name) {
            if (static_cast<unsigned char>(character) < 0x20 || character == 0x7f) character = ' ';
        }
        return name;
    } catch (...) {
        // The JWT claim is only a display hint. Server authentication remains authoritative.
        return {};
    }
}

} // namespace

ClientSession::ClientSession(ProtocolLaunchData *launchData) {
    if (launchData == nullptr) return;

    if (launchData->sessionToken.has_value()) {
        const std::string &source = *launchData->sessionToken;
        displayName_ = ReadDisplayName(source);
        token_.assign(source.begin(), source.end());
        SecureErase(*launchData->sessionToken);
        launchData->sessionToken.reset();
    }

    if (launchData->presenceToken.has_value()) {
        const std::string &source = *launchData->presenceToken;
        if (source.size() == 64 && std::all_of(source.begin(), source.end(), [](unsigned char c) {
                return std::isxdigit(c) != 0;
            })) {
            presenceToken_.assign(source.begin(), source.end());
        }
        SecureErase(*launchData->presenceToken);
        launchData->presenceToken.reset();
    }

    if (displayName_.empty()) displayName_ = "Player";

    if (launchData->worldId.has_value()) {
        worldId_ = std::move(*launchData->worldId);
        launchData->worldId.reset();
    }
    localServerPort_ = launchData->localServerPort;
    launchData->localServerPort.reset();

    // The parsed URL and query map contain duplicate token copies. They are no
    // longer needed after extracting session state, so scrub the raw URI and
    // remove credential entries from the generic query map.
    SecureErase(launchData->rawUrl);
    for (auto it = launchData->queryParameters.begin(); it != launchData->queryParameters.end();) {
        if (IsTokenParameter(it->first)) {
            SecureErase(it->second);
            it = launchData->queryParameters.erase(it);
        } else {
            ++it;
        }
    }
}

ClientSession::~ClientSession() {
    SecureErase(token_);
    SecureErase(presenceToken_);
}
