#pragma once

#include "MapDocument.hpp"

#include <string>
#include <string_view>

namespace blubox::studio {

class WebsiteApi {
public:
    WebsiteApi();

    const std::string& baseUrl() const { return baseUrl_; }
    bool PublishMap(const MapDocument& document, std::string_view accessToken, std::string& error) const;
    bool PublishGame(MapDocument& document, std::string_view title, std::string_view description,
                     std::string_view accessToken, std::string& result, std::string& error) const;
    bool DownloadMap(std::string_view worldId, std::string_view accessToken,
                     MapDocument& document, std::string& error) const;
    bool ValidateSession(std::string_view accessToken, std::string& displayName,
                         std::string& error) const;
    // Returns success for both a pending handoff and a redeemed session.
    // accessToken stays empty while the browser has not completed sign-in.
    bool PollStudioHandoff(std::string_view handoffId, std::string& accessToken,
                           std::string& error) const;

private:
    std::string baseUrl_;
};

} // namespace blubox::studio
