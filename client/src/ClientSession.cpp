#include "ClientSession.hpp"

#include <algorithm>
#include <utility>

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
    return key == "token" || key == "session_token" || key == "sessiontoken";
}

} // namespace

ClientSession::ClientSession(ProtocolLaunchData *launchData) {
    if (launchData == nullptr) return;

    if (launchData->sessionToken.has_value()) {
        const std::string &source = *launchData->sessionToken;
        token_.assign(source.begin(), source.end());
        SecureErase(*launchData->sessionToken);
        launchData->sessionToken.reset();
    }

    if (launchData->worldId.has_value()) {
        worldId_ = std::move(*launchData->worldId);
        launchData->worldId.reset();
    }

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
}
