#include "ProtocolLaunchData.hpp"

#include <cctype>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kBluboxScheme = "blubox://";

int HexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::string PercentDecode(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') {
            decoded.push_back(value[i]);
            continue;
        }

        if (i + 2 >= value.size()) {
            throw std::invalid_argument("Incomplete percent encoding in the blubox:// URL.");
        }

        const int high = HexValue(value[i + 1]);
        const int low = HexValue(value[i + 2]);
        if (high < 0 || low < 0) {
            throw std::invalid_argument("Invalid percent encoding in the blubox:// URL.");
        }

        const char decodedByte = static_cast<char>((high << 4) | low);
        if (decodedByte == '\0') {
            throw std::invalid_argument("The blubox:// URL cannot contain encoded NUL bytes.");
        }
        decoded.push_back(decodedByte);
        i += 2;
    }

    return decoded;
}

std::string LowerAscii(std::string value) {
    for (char &character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        character = static_cast<char>(std::tolower(byte));
    }
    return value;
}

void AssignUnique(std::optional<std::string> &field, const std::string &value,
                  const char *fieldName) {
    if (field.has_value()) {
        throw std::invalid_argument(std::string("The blubox:// URL contains multiple values for ") + fieldName + ".");
    }
    field = value;
}

} // namespace

ProtocolLaunchData ParseBluboxUri(const std::string &url) {
    std::string_view uri(url);
    if (uri.size() < kBluboxScheme.size() ||
        LowerAscii(std::string(uri.substr(0, kBluboxScheme.size()))) != kBluboxScheme) {
        throw std::invalid_argument("Invalid URL scheme: expected blubox://.");
    }

    ProtocolLaunchData data;
    data.rawUrl = url;

    uri.remove_prefix(kBluboxScheme.size());
    const std::size_t fragmentPosition = uri.find('#');
    if (fragmentPosition != std::string_view::npos) {
        uri = uri.substr(0, fragmentPosition);
    }

    const std::size_t queryPosition = uri.find('?');
    const std::string_view route = uri.substr(0, queryPosition);
    std::vector<std::string> routeSegments;
    std::size_t segmentStart = 0;
    while (segmentStart < route.size()) {
        while (segmentStart < route.size() && route[segmentStart] == '/') ++segmentStart;
        if (segmentStart >= route.size()) break;

        const std::size_t segmentEnd = route.find('/', segmentStart);
        const std::string_view segment = route.substr(
            segmentStart, segmentEnd == std::string_view::npos ? route.size() - segmentStart
                                                               : segmentEnd - segmentStart);
        routeSegments.push_back(PercentDecode(segment));
        if (segmentEnd == std::string_view::npos) break;
        segmentStart = segmentEnd + 1;
    }

    if (routeSegments.empty() || routeSegments.front().empty()) {
        throw std::invalid_argument("The blubox:// URL must specify an action, such as join or launch.");
    }

    data.action = LowerAscii(std::move(routeSegments.front()));
    for (std::size_t i = 1; i < routeSegments.size(); ++i) {
        if (!data.path.empty()) data.path.push_back('/');
        data.path += routeSegments[i];
    }

    // Support common path forms as well as query forms:
    // blubox://place/123 and blubox://join/123.
    if (!data.path.empty() && (data.action == "place" || data.action == "world" ||
                               data.action == "join" || data.action == "launch")) {
        const std::size_t pathEnd = data.path.find('/');
        const std::string pathWorldId = data.path.substr(0, pathEnd);
        if (!pathWorldId.empty()) AssignUnique(data.worldId, pathWorldId, "world ID");
    }

    if (queryPosition == std::string_view::npos) {
        return data;
    }

    const std::string_view query = uri.substr(queryPosition + 1);
    std::size_t parameterStart = 0;
    while (parameterStart <= query.size()) {
        const std::size_t parameterEnd = query.find('&', parameterStart);
        const std::string_view parameter = query.substr(
            parameterStart, parameterEnd == std::string_view::npos ? query.size() - parameterStart
                                                                    : parameterEnd - parameterStart);

        if (!parameter.empty()) {
            const std::size_t equalsPosition = parameter.find('=');
            const std::string key = LowerAscii(PercentDecode(parameter.substr(0, equalsPosition)));
            const std::string value = equalsPosition == std::string_view::npos
                ? std::string{}
                : PercentDecode(parameter.substr(equalsPosition + 1));

            if (key.empty()) {
                throw std::invalid_argument("The blubox:// URL contains an unnamed parameter.");
            }
            if (!data.queryParameters.emplace(key, value).second) {
                throw std::invalid_argument("The blubox:// URL contains a duplicate parameter: " + key + ".");
            }

            if (key == "token" || key == "session_token" || key == "sessiontoken") {
                AssignUnique(data.sessionToken, value, "session token");
            } else if (key == "presencetoken" || key == "presence_token") {
                AssignUnique(data.presenceToken, value, "presence token");
            } else if (key == "world" || key == "world_id" || key == "worldid" ||
                       key == "place" || key == "place_id" || key == "placeid" ||
                       key == "game_id" || key == "gameid" || key == "id") {
                if (data.worldId.has_value() && *data.worldId != value) {
                    throw std::invalid_argument("The blubox:// URL specifies conflicting world IDs.");
                }
                data.worldId = value;
            }
        }

        if (parameterEnd == std::string_view::npos) break;
        parameterStart = parameterEnd + 1;
    }

    return data;
}
