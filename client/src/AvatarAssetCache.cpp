#include "AvatarAssetCache.hpp"
#include "RaylibCompat.hpp"
#include "BluboxR6Character.hpp"
#include "BluboxR6JumpAnimation.hpp"
#include "BluboxR6WalkAnimation.hpp"
#include <nlohmann/json.hpp>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <raymath.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define DrawText Win32DrawText
#define DrawTextEx Win32DrawTextEx
#define LoadImage Win32LoadImage
#define PlaySound Win32PlaySound
#include <windows.h>
#include <winhttp.h>
#undef PlaySound
#undef LoadImage
#undef DrawTextEx
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle
#endif

namespace {
constexpr std::size_t kMaxTextureBytes = 5u * 1024u * 1024u;
constexpr std::size_t kMaxModelBytes = 30u * 1024u * 1024u;
constexpr std::string_view kAssetPath = "/storage/v1/object/public/marketplace-assets/";
constexpr float kAvatarTargetHeight = 2.24f;
constexpr float kGltfAnimationFrameSeconds = 0.017f;

#if defined(_WIN32)
std::wstring ToWide(std::string_view text) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

bool DownloadAsset(std::string_view url, std::size_t maximum, std::vector<unsigned char>& output) {
    if (!url.starts_with("https://") || url.size() > 2048 || url.find(kAssetPath) == std::string_view::npos ||
        url.find_first_of("\\\r\n") != std::string_view::npos || url.find('@') != std::string_view::npos) return false;
    const std::wstring wide = ToWide(url);
    if (wide.empty()) return false;
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwHostNameLength == 0) return false;
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    std::wstring target = path;
    if (parts.dwExtraInfoLength) target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HINTERNET session = WinHttpOpen(L"BluboxClient/AvatarAssets", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 3000, 3000, 5000, 5000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", target.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool ok = false;
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, statusSize = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) && status == 200) {
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) break;
                if (!available) { ok = !output.empty(); break; }
                if (output.size() + available > maximum) { output.clear(); break; }
                const auto old = output.size();
                output.resize(old + available);
                DWORD received = 0;
                if (!WinHttpReadData(request, output.data() + old, available, &received)) { output.clear(); break; }
                output.resize(old + received);
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}
#endif

std::string ImageFileType(const std::string& url, const std::vector<unsigned char>& bytes) {
    if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' &&
        bytes[2] == 'N' && bytes[3] == 'G' && bytes[4] == 0x0d &&
        bytes[5] == 0x0a && bytes[6] == 0x1a && bytes[7] == 0x0a) return ".png";
    if (bytes.size() >= 3 && bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff) return ".jpg";
    if (bytes.size() >= 6 && std::string_view(reinterpret_cast<const char*>(bytes.data()), 6).starts_with("GIF8")) return ".gif";
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') return ".bmp";
    if (bytes.size() >= 4 && std::string_view(reinterpret_cast<const char*>(bytes.data()), 4) == "qoif") return ".qoi";
    if (bytes.size() >= 4 && std::string_view(reinterpret_cast<const char*>(bytes.data()), 4) == "DDS ") return ".dds";
    if (bytes.size() >= 4 && std::string_view(reinterpret_cast<const char*>(bytes.data()), 4) == "8BPS") return ".psd";

    const std::size_t suffixEnd = url.find_first_of("?#");
    const std::string_view path(url.data(), suffixEnd == std::string::npos ? url.size() : suffixEnd);
    const std::size_t extensionAt = path.find_last_of('.');
    if (extensionAt == std::string_view::npos) return ".png";
    std::string extension(path.substr(extensionAt));
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
        extension == ".bmp" || extension == ".tga" || extension == ".gif" ||
        extension == ".qoi" || extension == ".psd" || extension == ".dds" ||
        extension == ".hdr" || extension == ".ktx" || extension == ".astc" ||
        extension == ".pkm" || extension == ".pvr") return extension;
    return ".png";
}

Color ParseColor(std::string_view value) {
    if (value.size() != 7 || value[0] != '#') return Color{255, 255, 255, 255};
    unsigned int rgb = 0;
    for (std::size_t i = 1; i < value.size(); ++i) {
        const char c = value[i];
        const unsigned int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
        rgb = (rgb << 4) | digit;
    }
    return Color{static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8), static_cast<unsigned char>(rgb), 255};
}

struct AccessoryFit {
    Vector3 position{};
    Vector3 center{};
    Vector3 halfSize{};
    float scale = 0.0f;
};

AccessoryFit FitAccessory(const BoundingBox& bounds,
                         const NetworkPlayerSnapshot::AccessoryAsset& accessory) {
    const Vector3 size{bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y,
                       bounds.max.z - bounds.min.z};
    const float largest = std::max({size.x, size.y, size.z});
    if (!(largest > 0.00001f) || !std::isfinite(largest)) return {};
    const Vector3 center{(bounds.min.x + bounds.max.x) * 0.5f,
                         (bounds.min.y + bounds.max.y) * 0.5f,
                         (bounds.min.z + bounds.max.z) * 0.5f};

    std::string name = accessory.attachmentName;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const bool hat = name.ends_with("hatattachment") || name.ends_with("hairattachment");
    const bool back = name.find("back") != std::string::npos;
    const bool face = name.find("face") != std::string::npos;
    const bool shoulder = name.find("shoulder") != std::string::npos;
    const bool waist = name.find("waist") != std::string::npos;
    const float desiredSize = hat ? 0.50f : shoulder ? 0.40f : back ? 0.68f : waist ? 0.50f : face ? 0.32f : 0.44f;
    const float scale = std::min(desiredSize / largest, 1000.0f) *
        std::clamp(std::isfinite(accessory.scale) ? accessory.scale : 1.0f, 0.05f, 5.0f);
    Vector3 anchor{0.0f, 1.93f, face ? 0.34f : 0.31f};
    if (hat) anchor = Vector3{0.0f, 2.24f, 0.0f};
    else if (back) anchor = Vector3{0.0f, 1.22f, -0.34f};
    else if (shoulder) anchor = Vector3{name.find("left") != std::string::npos ? -0.58f : 0.58f, 1.28f, 0.0f};
    else if (waist) anchor = Vector3{0.0f, 0.10f, 0.0f};

    const Vector3 position{anchor.x - center.x * scale + accessory.offsetX,
        (hat ? anchor.y - bounds.min.y * scale : anchor.y - center.y * scale) + accessory.offsetY,
        anchor.z - center.z * scale + accessory.offsetZ};
    return {position,
        Vector3{position.x + center.x * scale, position.y + center.y * scale,
                position.z + center.z * scale},
        Vector3{size.x * scale * 0.5f, size.y * scale * 0.5f, size.z * scale * 0.5f},
        scale};
}

void DrawShadowQuad(Texture2D texture, Vector3 ground, Vector3 center,
                    float radiusX, float radiusZ, float yaw, unsigned char opacity) {
    if (!texture.id || radiusX <= 0.01f || radiusZ <= 0.01f) return;
    constexpr float lift = 0.018f;
    const float y = ground.y + lift;
    const float c = std::cos(yaw), s = std::sin(yaw);
    const auto vertex = [&](float x, float z, float u, float v) {
        rlTexCoord2f(u, v);
        rlVertex3f(center.x + x * c - z * s, y, center.z + x * s + z * c);
    };
    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, opacity);
    rlNormal3f(0.0f, 1.0f, 0.0f);
    vertex(-radiusX, -radiusZ, 0.0f, 0.0f);
    vertex(radiusX, -radiusZ, 1.0f, 0.0f);
    vertex(radiusX, radiusZ, 1.0f, 1.0f);
    vertex(-radiusX, radiusZ, 0.0f, 1.0f);
    rlEnd();
    rlSetTexture(0);
}

Vector3 RotateAvatarLocal(Vector3 value, float yaw) {
    const float rotation = PI - yaw;
    const float c = std::cos(rotation), s = std::sin(rotation);
    return Vector3{value.x * c + value.z * s, value.y,
                   -value.x * s + value.z * c};
}

void DrawOverlay(Texture2D texture, Rectangle crop, float centerX, float centerY,
                 float width, float height, float frontZ, Color tint) {
    if (!texture.id || texture.width <= 0 || texture.height <= 0) return;
    const float u0 = crop.x / static_cast<float>(texture.width);
    const float v0 = crop.y / static_cast<float>(texture.height);
    const float u1 = (crop.x + crop.width) / static_cast<float>(texture.width);
    const float v1 = (crop.y + crop.height) / static_cast<float>(texture.height);
    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlNormal3f(0.0f, 0.0f, 1.0f);
    rlTexCoord2f(u0, v1); rlVertex3f(centerX - width * .5f, centerY - height * .5f, frontZ);
    rlTexCoord2f(u1, v1); rlVertex3f(centerX + width * .5f, centerY - height * .5f, frontZ);
    rlTexCoord2f(u1, v0); rlVertex3f(centerX + width * .5f, centerY + height * .5f, frontZ);
    rlTexCoord2f(u0, v0); rlVertex3f(centerX - width * .5f, centerY + height * .5f, frontZ);
    rlEnd();
    rlSetTexture(0);
}

void DrawCurvedOverlay(Texture2D texture, float centerX, float centerY,
                       float width, float height, float frontZ, Color tint) {
    if (!texture.id || texture.width <= 0 || texture.height <= 0) return;

    constexpr int horizontalSegments = 12;
    constexpr int verticalSegments = 8;
    constexpr float horizontalCurvature = 0.02f;
    constexpr float verticalCurvature = 0.005f;
    const auto vertex = [&](float x, float y, float u, float v) {
        const float localX = centerX + x * width * 0.5f;
        const float localY = centerY + y * height * 0.5f;
        const float depth = frontZ - horizontalCurvature * x * x -
                            verticalCurvature * y * y;
        const Vector3 normal{
            2.0f * horizontalCurvature * x / (width * 0.5f),
            2.0f * verticalCurvature * y / (height * 0.5f),
            1.0f};
        const float normalLength = std::sqrt(normal.x * normal.x + normal.y * normal.y +
                                             normal.z * normal.z);
        rlNormal3f(normal.x / normalLength, normal.y / normalLength, normal.z / normalLength);
        rlTexCoord2f(u, v);
        rlVertex3f(localX, localY, depth);
    };

    rlSetTexture(texture.id);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    for (int row = 0; row < verticalSegments; ++row) {
        const float top = 1.0f - 2.0f * static_cast<float>(row) / verticalSegments;
        const float bottom = 1.0f - 2.0f * static_cast<float>(row + 1) / verticalSegments;
        const float topV = static_cast<float>(row) / verticalSegments;
        const float bottomV = static_cast<float>(row + 1) / verticalSegments;
        for (int column = 0; column < horizontalSegments; ++column) {
            const float left = 2.0f * static_cast<float>(column) / horizontalSegments - 1.0f;
            const float right = 2.0f * static_cast<float>(column + 1) / horizontalSegments - 1.0f;
            const float leftU = static_cast<float>(column) / horizontalSegments;
            const float rightU = static_cast<float>(column + 1) / horizontalSegments;

            vertex(left, bottom, leftU, bottomV);
            vertex(right, bottom, rightU, bottomV);
            vertex(right, top, rightU, topV);
            vertex(left, bottom, leftU, bottomV);
            vertex(right, top, rightU, topV);
            vertex(left, top, leftU, topV);
        }
    }
    rlEnd();
    rlSetTexture(0);
}

bool ParseGlbJson(const unsigned char *data, std::size_t size, nlohmann::json &output) {
    if (data == nullptr || size < 20 || data[0] != 'g' || data[1] != 'l' ||
        data[2] != 'T' || data[3] != 'F') return false;
    const std::uint32_t jsonLength =
        static_cast<std::uint32_t>(data[12]) |
        (static_cast<std::uint32_t>(data[13]) << 8) |
        (static_cast<std::uint32_t>(data[14]) << 16) |
        (static_cast<std::uint32_t>(data[15]) << 24);
    const std::uint32_t jsonType =
        static_cast<std::uint32_t>(data[16]) |
        (static_cast<std::uint32_t>(data[17]) << 8) |
        (static_cast<std::uint32_t>(data[18]) << 16) |
        (static_cast<std::uint32_t>(data[19]) << 24);
    if (jsonType != 0x4E4F534Au || jsonLength > size - 20) return false;
    try {
        const auto *begin = reinterpret_cast<const char *>(data + 20);
        output = nlohmann::json::parse(begin, begin + jsonLength);
    } catch (const std::exception &) {
        return false;
    }
    return output.is_object();
}

bool ReadGltfVector(const nlohmann::json &node, const char *key,
                    float *values, std::size_t count, const float *fallback) {
    if (!node.contains(key)) {
        std::copy(fallback, fallback + count, values);
        return true;
    }
    const auto &array = node[key];
    if (!array.is_array() || array.size() != count) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!array[i].is_number()) return false;
        values[i] = array[i].get<float>();
        if (!std::isfinite(values[i])) return false;
    }
    return true;
}

bool GltfNodeWorldTransforms(const nlohmann::json &gltf, std::vector<Matrix> &worldTransforms,
                             std::vector<Matrix> &meshWorldTransforms) {
    if (!gltf.contains("nodes") || !gltf["nodes"].is_array()) return false;
    const auto &nodes = gltf["nodes"];
    std::vector<int> parents(nodes.size(), -1);
    for (std::size_t parent = 0; parent < nodes.size(); ++parent) {
        if (!nodes[parent].is_object() || !nodes[parent].contains("children")) continue;
        const auto &children = nodes[parent]["children"];
        if (!children.is_array()) return false;
        for (const auto &child : children) {
            if (!child.is_number_unsigned()) return false;
            const std::size_t index = child.get<std::size_t>();
            if (index >= nodes.size() || parents[index] != -1) return false;
            parents[index] = static_cast<int>(parent);
        }
    }

    worldTransforms.assign(nodes.size(), MatrixIdentity());
    std::vector<bool> resolved(nodes.size(), false);
    std::vector<bool> resolving(nodes.size(), false);
    const auto resolve = [&](const auto &self, std::size_t index) -> bool {
        if (resolved[index]) return true;
        if (resolving[index]) return false;
        resolving[index] = true;
        const auto &node = nodes[index];
        Matrix local = MatrixIdentity();
        if (!node.is_object()) return false;
        if (node.contains("matrix")) {
            const auto &matrix = node["matrix"];
            if (!matrix.is_array() || matrix.size() != 16) return false;
            float values[16]{};
            for (std::size_t i = 0; i < 16; ++i) {
                if (!matrix[i].is_number()) return false;
                values[i] = matrix[i].get<float>();
                if (!std::isfinite(values[i])) return false;
            }
            local = Matrix{
                values[0], values[4], values[8], values[12],
                values[1], values[5], values[9], values[13],
                values[2], values[6], values[10], values[14],
                values[3], values[7], values[11], values[15]};
        } else {
            const float defaultTranslation[3]{0.0f, 0.0f, 0.0f};
            const float defaultRotation[4]{0.0f, 0.0f, 0.0f, 1.0f};
            const float defaultScale[3]{1.0f, 1.0f, 1.0f};
            float translation[3]{};
            float rotation[4]{};
            float scale[3]{};
            if (!ReadGltfVector(node, "translation", translation, 3, defaultTranslation) ||
                !ReadGltfVector(node, "rotation", rotation, 4, defaultRotation) ||
                !ReadGltfVector(node, "scale", scale, 3, defaultScale)) return false;
            local = MatrixMultiply(
                MatrixMultiply(MatrixTranslate(translation[0], translation[1], translation[2]),
                               QuaternionToMatrix(Quaternion{rotation[0], rotation[1],
                                                             rotation[2], rotation[3]})),
                MatrixScale(scale[0], scale[1], scale[2]));
        }
        const int parent = parents[index];
        if (parent >= 0) {
            if (!self(self, static_cast<std::size_t>(parent))) return false;
            worldTransforms[index] = MatrixMultiply(
                worldTransforms[static_cast<std::size_t>(parent)], local);
        } else {
            worldTransforms[index] = local;
        }
        resolving[index] = false;
        resolved[index] = true;
        return true;
    };
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (!resolve(resolve, i)) return false;
    }

    meshWorldTransforms.clear();
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto &node = nodes[i];
        if (!node.contains("mesh")) continue;
        if (!node["mesh"].is_number_unsigned()) return false;
        const std::size_t meshIndex = node["mesh"].get<std::size_t>();
        if (!gltf.contains("meshes") || !gltf["meshes"].is_array() ||
            meshIndex >= gltf["meshes"].size()) return false;
        const auto &mesh = gltf["meshes"][meshIndex];
        if (!mesh.is_object() || !mesh.contains("primitives") ||
            !mesh["primitives"].is_array()) return false;
        for (const auto &primitive : mesh["primitives"]) {
            if (primitive.contains("mode") && primitive["mode"].is_number_integer() &&
                primitive["mode"].get<int>() != 4) continue;
            meshWorldTransforms.push_back(worldTransforms[i]);
        }
    }
    return !meshWorldTransforms.empty();
}

bool BuildAnimationVertexMappings(
    const Model &character, const Model &animationSource,
    const std::vector<Matrix> &sourceMeshWorldTransforms,
    std::vector<Matrix> &baseToAnimationSpace,
    std::vector<Matrix> &animationToBaseSpace,
    std::vector<int> &animationSourceMeshIndices,
    std::vector<std::vector<int>> &animationSourceVertexIndices,
    int &headMeshIndex) {
    if (!character.meshes || character.meshCount <= 0 || !animationSource.meshes ||
        animationSource.meshCount <= 0 || animationSource.boneCount <= 0 ||
        !animationSource.bones || !animationSource.bindPose ||
        sourceMeshWorldTransforms.size() != static_cast<std::size_t>(animationSource.meshCount))
        return false;

    const Matrix baseToGltf = MatrixRotateX(-PI * 0.5f);
    struct MeshTransfer {
        std::vector<int> vertexIndices;
        Matrix toAnimation{};
        Matrix fromAnimation{};
        int sourceMeshIndex = -1;
    };
    std::vector<MeshTransfer> transfers(static_cast<std::size_t>(character.meshCount));

    for (int characterMeshIndex = 0; characterMeshIndex < character.meshCount;
         ++characterMeshIndex) {
        const Mesh &characterMesh = character.meshes[characterMeshIndex];
        if (!characterMesh.vertices || characterMesh.vertexCount <= 0) return false;
        MeshTransfer bestTransfer;
        float bestAverageDistance = std::numeric_limits<float>::max();
        float bestMaximumDistance = std::numeric_limits<float>::max();
        bool foundTransfer = false;

        for (int sourceMeshIndex = 0; sourceMeshIndex < animationSource.meshCount;
             ++sourceMeshIndex) {
            const Mesh &sourceMesh = animationSource.meshes[sourceMeshIndex];
            if (sourceMesh.vertexCount <= 0 || !sourceMesh.vertices ||
                !sourceMesh.boneIds || !sourceMesh.boneWeights || !sourceMesh.animVertices)
                continue;

            const Matrix toAnimation = MatrixMultiply(
                sourceMeshWorldTransforms[static_cast<std::size_t>(sourceMeshIndex)], baseToGltf);
            const Matrix fromAnimation = MatrixInvert(toAnimation);
            MeshTransfer candidate;
            candidate.toAnimation = toAnimation;
            candidate.fromAnimation = fromAnimation;
            candidate.sourceMeshIndex = sourceMeshIndex;
            candidate.vertexIndices.resize(static_cast<std::size_t>(characterMesh.vertexCount));
            double distanceTotal = 0.0;
            float maximumDistance = 0.0f;
            bool valid = true;

            for (int vertexIndex = 0; vertexIndex < characterMesh.vertexCount; ++vertexIndex) {
                const Vector3 basePosition{
                    characterMesh.vertices[vertexIndex * 3],
                    characterMesh.vertices[vertexIndex * 3 + 1],
                    characterMesh.vertices[vertexIndex * 3 + 2]};
                const Vector3 sourcePosition =
                    Vector3Transform(basePosition, toAnimation);
                int nearestIndex = -1;
                float nearestDistance = std::numeric_limits<float>::max();
                for (int sourceVertexIndex = 0; sourceVertexIndex < sourceMesh.vertexCount;
                     ++sourceVertexIndex) {
                    const Vector3 sourceVertex{
                        sourceMesh.vertices[sourceVertexIndex * 3],
                        sourceMesh.vertices[sourceVertexIndex * 3 + 1],
                        sourceMesh.vertices[sourceVertexIndex * 3 + 2]};
                    const float distance = Vector3Distance(sourcePosition, sourceVertex);
                    if (distance < nearestDistance) {
                        nearestDistance = distance;
                        nearestIndex = sourceVertexIndex;
                    }
                }
                if (nearestIndex < 0 || nearestDistance > 0.0001f) {
                    valid = false;
                    break;
                }
                maximumDistance = std::max(maximumDistance, nearestDistance);
                distanceTotal += nearestDistance;
                candidate.vertexIndices[static_cast<std::size_t>(vertexIndex)] = nearestIndex;
            }
            if (!valid) continue;
            const float averageDistance =
                static_cast<float>(distanceTotal / characterMesh.vertexCount);
            if (averageDistance < bestAverageDistance) {
                bestAverageDistance = averageDistance;
                bestMaximumDistance = maximumDistance;
                bestTransfer = std::move(candidate);
                foundTransfer = true;
            }
        }
        if (!foundTransfer || bestMaximumDistance > 0.0001f) return false;
        transfers[static_cast<std::size_t>(characterMeshIndex)] = std::move(bestTransfer);
    }

    float highestMeshBottom = -std::numeric_limits<float>::max();
    int highestMeshIndex = -1;
    for (int meshIndex = 0; meshIndex < character.meshCount; ++meshIndex) {
        const Mesh &mesh = character.meshes[meshIndex];
        float bottom = std::numeric_limits<float>::max();
        for (int vertexIndex = 0; vertexIndex < mesh.vertexCount; ++vertexIndex)
            bottom = std::min(bottom, mesh.vertices[vertexIndex * 3 + 1]);
        if (bottom > highestMeshBottom) {
            highestMeshBottom = bottom;
            highestMeshIndex = meshIndex;
        }
    }

    baseToAnimationSpace.clear();
    animationToBaseSpace.clear();
    animationSourceMeshIndices.clear();
    animationSourceVertexIndices.clear();
    for (const MeshTransfer &transfer : transfers) {
        baseToAnimationSpace.push_back(transfer.toAnimation);
        animationToBaseSpace.push_back(transfer.fromAnimation);
        animationSourceMeshIndices.push_back(transfer.sourceMeshIndex);
        animationSourceVertexIndices.push_back(transfer.vertexIndices);
    }
    headMeshIndex = highestMeshIndex;
    return true;
}

bool UpdateRetargetedAnimation(Model character, Model animationSource,
                               ModelAnimation animation, int frame,
                               const BoundingBox &bindBounds,
                               const std::vector<Matrix> &baseToAnimationSpace,
                               const std::vector<Matrix> &animationToBaseSpace,
                               const std::vector<int> &animationSourceMeshIndices,
                               const std::vector<std::vector<int>> &animationSourceVertexIndices) {
    const auto uploadMesh = [](Mesh &mesh) {
        if (mesh.vboId[0] && mesh.animVertices) {
            rlUpdateVertexBuffer(mesh.vboId[0], mesh.animVertices,
                                 mesh.vertexCount * 3 * static_cast<int>(sizeof(float)), 0);
        }
        if (mesh.vboId[2] && mesh.normals && mesh.animNormals) {
            rlUpdateVertexBuffer(mesh.vboId[2], mesh.animNormals,
                                 mesh.vertexCount * 3 * static_cast<int>(sizeof(float)), 0);
        }
    };
    const auto restoreBindPose = [&] {
        for (int meshIndex = 0; meshIndex < character.meshCount; ++meshIndex) {
            Mesh &mesh = character.meshes[meshIndex];
            if (mesh.animVertices && mesh.vertices) {
                std::memcpy(mesh.animVertices, mesh.vertices,
                            static_cast<std::size_t>(mesh.vertexCount) * 3 * sizeof(float));
            }
            if (mesh.normals && mesh.animNormals) {
                std::memcpy(mesh.animNormals, mesh.normals,
                            static_cast<std::size_t>(mesh.vertexCount) * 3 * sizeof(float));
            }
            uploadMesh(mesh);
        }
    };
    if (frame < 0 || frame >= animation.frameCount ||
        baseToAnimationSpace.size() != static_cast<std::size_t>(character.meshCount) ||
        animationToBaseSpace.size() != static_cast<std::size_t>(character.meshCount) ||
        animationSourceMeshIndices.size() != static_cast<std::size_t>(character.meshCount) ||
        animationSourceVertexIndices.size() != static_cast<std::size_t>(character.meshCount)) {
        restoreBindPose();
        return false;
    }
    UpdateModelAnimation(animationSource, animation, frame);
    const float margin = (bindBounds.max.y - bindBounds.min.y) * 0.5f;
    const Vector3 minAllowed{bindBounds.min.x - margin, bindBounds.min.y - margin,
                             bindBounds.min.z - margin};
    const Vector3 maxAllowed{bindBounds.max.x + margin, bindBounds.max.y + margin,
                             bindBounds.max.z + margin};
    bool validPose = true;
    for (int meshIndex = 0; meshIndex < character.meshCount; ++meshIndex) {
        Mesh &mesh = character.meshes[meshIndex];
        const Matrix &fromAnimation = animationToBaseSpace[static_cast<std::size_t>(meshIndex)];
        const int sourceMeshIndex =
            animationSourceMeshIndices[static_cast<std::size_t>(meshIndex)];
        if (!mesh.animVertices || !mesh.vertices) {
            restoreBindPose();
            return false;
        }
        if (sourceMeshIndex < 0 || sourceMeshIndex >= animationSource.meshCount) {
            restoreBindPose();
            return false;
        }
        const Mesh &sourceMesh = animationSource.meshes[sourceMeshIndex];
        const std::vector<int> &sourceVertexIndices =
            animationSourceVertexIndices[static_cast<std::size_t>(meshIndex)];
        if (!sourceMesh.animVertices ||
            sourceVertexIndices.size() != static_cast<std::size_t>(mesh.vertexCount)) {
            restoreBindPose();
            return false;
        }
        Matrix fromAnimationNormal = fromAnimation;
        fromAnimationNormal.m12 = fromAnimationNormal.m13 = fromAnimationNormal.m14 = 0.0f;
        for (int vertexIndex = 0; vertexIndex < mesh.vertexCount; ++vertexIndex) {
            const int sourceVertexIndex =
                sourceVertexIndices[static_cast<std::size_t>(vertexIndex)];
            if (sourceVertexIndex < 0 || sourceVertexIndex >= sourceMesh.vertexCount) {
                restoreBindPose();
                return false;
            }
            const Vector3 sourcePosition{
                sourceMesh.animVertices[sourceVertexIndex * 3],
                sourceMesh.animVertices[sourceVertexIndex * 3 + 1],
                sourceMesh.animVertices[sourceVertexIndex * 3 + 2]};
            const Vector3 animatedPosition = Vector3Transform(sourcePosition, fromAnimation);
            if (!std::isfinite(animatedPosition.x) || !std::isfinite(animatedPosition.y) ||
                !std::isfinite(animatedPosition.z) || animatedPosition.x < minAllowed.x ||
                animatedPosition.y < minAllowed.y || animatedPosition.z < minAllowed.z ||
                animatedPosition.x > maxAllowed.x || animatedPosition.y > maxAllowed.y ||
                animatedPosition.z > maxAllowed.z) {
                validPose = false;
            }
            mesh.animVertices[vertexIndex * 3] = animatedPosition.x;
            mesh.animVertices[vertexIndex * 3 + 1] = animatedPosition.y;
            mesh.animVertices[vertexIndex * 3 + 2] = animatedPosition.z;

            if (mesh.normals) {
                const Vector3 animatedNormal = sourceMesh.animNormals
                    ? Vector3Transform(Vector3{
                          sourceMesh.animNormals[sourceVertexIndex * 3],
                          sourceMesh.animNormals[sourceVertexIndex * 3 + 1],
                          sourceMesh.animNormals[sourceVertexIndex * 3 + 2]},
                          fromAnimationNormal)
                    : Vector3{mesh.normals[vertexIndex * 3],
                              mesh.normals[vertexIndex * 3 + 1],
                              mesh.normals[vertexIndex * 3 + 2]};
                mesh.animNormals[vertexIndex * 3] = animatedNormal.x;
                mesh.animNormals[vertexIndex * 3 + 1] = animatedNormal.y;
                mesh.animNormals[vertexIndex * 3 + 2] = animatedNormal.z;
            }
        }
    }
    if (!validPose) {
        restoreBindPose();
        return false;
    }
    for (int meshIndex = 0; meshIndex < character.meshCount; ++meshIndex)
        uploadMesh(character.meshes[meshIndex]);
    return true;
}
} // namespace

AvatarAssetCache::~AvatarAssetCache() { Unload(); }

bool AvatarAssetCache::LoadBaseCharacter(const Shader& lightingShader) {
    if (baseCharacterLoaded_) return baseCharacter_.meshCount > 0;
    baseCharacterLoaded_ = true;
    lightingShader_ = lightingShader;
#if defined(_WIN32)
    if (blubox::kR6CharacterDataSize < 20 ||
        blubox::kR6CharacterData[0] != 'g' || blubox::kR6CharacterData[1] != 'l' ||
        blubox::kR6CharacterData[2] != 'T' || blubox::kR6CharacterData[3] != 'F') {
        TraceLog(LOG_WARNING, "Embedded Blubox R6 character asset is invalid.");
        return false;
    }

    nlohmann::json gltf;
    const std::uint32_t jsonLength =
        static_cast<std::uint32_t>(blubox::kR6CharacterData[12]) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[13]) << 8) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[14]) << 16) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[15]) << 24);
    const std::uint32_t jsonType =
        static_cast<std::uint32_t>(blubox::kR6CharacterData[16]) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[17]) << 8) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[18]) << 16) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[19]) << 24);
    if (jsonType == 0x4E4F534Au && jsonLength <= static_cast<std::uint32_t>(blubox::kR6CharacterDataSize - 20)) {
        try {
            const auto* jsonBegin = reinterpret_cast<const char*>(blubox::kR6CharacterData + 20);
            gltf = nlohmann::json::parse(jsonBegin, jsonBegin + jsonLength);
        } catch (const std::exception&) {
            TraceLog(LOG_WARNING, "Embedded Blubox R6 character metadata could not be parsed.");
        }
    }
    std::error_code error;
    const auto tempRoot = std::filesystem::temp_directory_path(error);
    if (error) return false;
    const auto tempPath = tempRoot / ("blubox-r6-character-" +
        std::to_string(static_cast<unsigned long long>(GetCurrentProcessId())) + ".glb");
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output || !output.write(reinterpret_cast<const char*>(blubox::kR6CharacterData),
                                     blubox::kR6CharacterDataSize)) {
            std::filesystem::remove(tempPath, error);
            TraceLog(LOG_WARNING, "Could not stage embedded Blubox R6 character asset.");
            return false;
        }
    }
    baseCharacter_ = LoadModel(tempPath.string().c_str());
    std::filesystem::remove(tempPath, error);
    if (baseCharacter_.meshCount <= 0 || baseCharacter_.materialCount <= 0) {
        if (baseCharacter_.meshCount > 0) UnloadModel(baseCharacter_);
        baseCharacter_ = Model{};
        TraceLog(LOG_WARNING, "Raylib could not load the embedded Blubox R6 character.");
        return false;
    }

    if (gltf.is_object() && gltf.contains("materials") && gltf["materials"].is_array()) {
        for (std::size_t index = 0; index < gltf["materials"].size(); ++index) {
            const auto& material = gltf["materials"][index];
            if (!material.is_object() || !material.contains("name") || !material["name"].is_string()) continue;
            // Raylib reserves material slot 0 for its fallback material; glTF
            // material n is therefore stored in the model at slot n + 1.
            if (index + 1 < static_cast<std::size_t>(baseCharacter_.materialCount))
                baseCharacterMaterials_[material["name"].get<std::string>()] = static_cast<int>(index + 1);
        }
    }
    baseCharacterBounds_ = GetModelBoundingBox(baseCharacter_);
    const float height = baseCharacterBounds_.max.y - baseCharacterBounds_.min.y;
    if (!(height > 0.0001f) || !std::isfinite(height)) {
        UnloadModel(baseCharacter_);
        baseCharacter_ = Model{};
        baseCharacterMaterials_.clear();
        return false;
    }
    baseCharacterScale_ = kAvatarTargetHeight / height;
    if (lightingShader.id != 0) {
        for (int i = 0; i < baseCharacter_.materialCount; ++i)
            baseCharacter_.materials[i].shader = lightingShader;
    }

    if (!error) {
        const auto processId = static_cast<unsigned long long>(GetCurrentProcessId());
        const auto walkPath = tempRoot / ("blubox-r6-walk-" + std::to_string(processId) + ".glb");
        const auto jumpPath = tempRoot / ("blubox-r6-jump-" + std::to_string(processId) + ".glb");
        const auto writeAsset = [](const std::filesystem::path &path,
                                   const unsigned char *data, std::size_t size) {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            return output && output.write(reinterpret_cast<const char *>(data),
                                          static_cast<std::streamsize>(size));
        };
        const auto validGlb = [](const unsigned char *data, std::size_t size) {
            return size >= 20 && data[0] == 'g' && data[1] == 'l' &&
                   data[2] == 'T' && data[3] == 'F';
        };
        const bool assetsStaged = validGlb(blubox::kR6WalkAnimationData,
                                           blubox::kR6WalkAnimationDataSize) &&
            validGlb(blubox::kR6JumpAnimationData, blubox::kR6JumpAnimationDataSize) &&
            writeAsset(walkPath, blubox::kR6WalkAnimationData, blubox::kR6WalkAnimationDataSize) &&
            writeAsset(jumpPath, blubox::kR6JumpAnimationData, blubox::kR6JumpAnimationDataSize);
        Model animationSource{};
        bool animationReady = false;
        if (assetsStaged) {
            animationSource = LoadModel(walkPath.string().c_str());
            walkAnimations_ = LoadModelAnimations(walkPath.string().c_str(), &walkAnimationCount_);
            jumpAnimations_ = LoadModelAnimations(jumpPath.string().c_str(), &jumpAnimationCount_);
        } else {
            TraceLog(LOG_WARNING, "Could not stage R6 animation clips; keeping the base character unchanged.");
        }
        const auto animationMatchesModel = [](const Model &model,
                                              const ModelAnimation *animations, int count) {
            if (model.boneCount <= 0 || !model.bones || !animations || count <= 0) return false;
            for (int animationIndex = 0; animationIndex < count; ++animationIndex) {
                const ModelAnimation &animation = animations[animationIndex];
                if (animation.frameCount <= 0 || animation.boneCount != model.boneCount ||
                    !animation.bones || !animation.framePoses) return false;
                for (int boneIndex = 0; boneIndex < model.boneCount; ++boneIndex) {
                    if (std::strcmp(model.bones[boneIndex].name,
                                    animation.bones[boneIndex].name) != 0)
                        return false;
                }
            }
            return true;
        };

        if (assetsStaged && animationSource.meshCount > 0 &&
            animationMatchesModel(animationSource, walkAnimations_, walkAnimationCount_) &&
            animationMatchesModel(animationSource, jumpAnimations_, jumpAnimationCount_)) {
            nlohmann::json walkGltf;
            std::vector<Matrix> nodeWorldTransforms;
            std::vector<Matrix> sourceMeshWorldTransforms;
            if (ParseGlbJson(blubox::kR6WalkAnimationData,
                             blubox::kR6WalkAnimationDataSize, walkGltf) &&
                GltfNodeWorldTransforms(walkGltf, nodeWorldTransforms,
                                        sourceMeshWorldTransforms) &&
                BuildAnimationVertexMappings(baseCharacter_, animationSource,
                                             sourceMeshWorldTransforms,
                                             baseToAnimationSpace_,
                                             animationToBaseSpace_,
                                             animationSourceMeshIndices_,
                                             animationSourceVertexIndices_,
                                             headMeshIndex_)) {
                animationSource_ = animationSource;
                animationSource = Model{};
                animationReady = true;
            }
        }
        if (animationReady) {
            TraceLog(LOG_INFO,
                     "Retargeted R6 animation clips onto the existing character (%d bones, walk %d frames, jump %d frames).",
                     animationSource_.boneCount, walkAnimations_[0].frameCount,
                     jumpAnimations_[0].frameCount);
        } else {
            TraceLog(LOG_WARNING,
                     "Could not retarget R6 animation clips onto the existing character; the original character will remain unchanged.");
            if (walkAnimations_) UnloadModelAnimations(walkAnimations_, walkAnimationCount_);
            walkAnimations_ = nullptr;
            walkAnimationCount_ = 0;
            if (jumpAnimations_) UnloadModelAnimations(jumpAnimations_, jumpAnimationCount_);
            jumpAnimations_ = nullptr;
            jumpAnimationCount_ = 0;
        }
        if (animationSource.meshCount > 0) UnloadModel(animationSource);
        if (!animationReady) {
            baseToAnimationSpace_.clear();
            animationToBaseSpace_.clear();
            animationSourceMeshIndices_.clear();
            animationSourceVertexIndices_.clear();
            headMeshIndex_ = -1;
        }
        std::error_code ignored;
        std::filesystem::remove(walkPath, ignored);
        std::filesystem::remove(jumpPath, ignored);
    } else {
        TraceLog(LOG_WARNING, "Could not locate a temporary directory for animated R6 assets.");
    }

    TraceLog(LOG_INFO, "Loaded Blubox R6 character (%d meshes, %.2f units tall).",
             baseCharacter_.meshCount, height * baseCharacterScale_);
    return true;
#else
    (void)lightingShader;
    return false;
#endif
}

bool AvatarAssetCache::DrawBaseCharacter(Vector3 feetPosition,
                                         const NetworkPlayerSnapshot& avatar,
                                         float facingYaw, CharacterAnimation animation,
                                         float animationTimeSeconds) {
    if (!baseCharacter_.meshCount || baseCharacterScale_ <= 0.0f) return false;
    const Color skin = ParseColor(avatar.bodyColor);
    const Color shirt = ParseColor(avatar.shirtColor);
    const Color pants = ParseColor(avatar.pantsColor);
    for (const auto& [name, index] : baseCharacterMaterials_) {
        if (index < 0 || index >= baseCharacter_.materialCount) continue;
        Color tint = skin;
        if (name == "ShirtMaterial") tint = shirt;
        else if (name == "PantsMaterial") tint = pants;
        baseCharacter_.materials[index].maps[MATERIAL_MAP_DIFFUSE].color = tint;
    }

    bool animationApplied = false;
    if (!animationRetargetingDisabled_ && animationSource_.boneCount > 0) {
        const ModelAnimation *activeAnimation = walkAnimations_;
        int activeAnimationCount = walkAnimationCount_;
        if (animation == CharacterAnimation::Jump) {
            activeAnimation = jumpAnimations_;
            activeAnimationCount = jumpAnimationCount_;
        }
        if (activeAnimation && activeAnimationCount > 0) {
            const ModelAnimation &clip = activeAnimation[0];
            const float duration = static_cast<float>(std::max(1, clip.frameCount - 1)) *
                                   kGltfAnimationFrameSeconds;
            const float cycleTime = std::fmod(std::max(0.0f, animationTimeSeconds), duration);
            const int frame = animation == CharacterAnimation::Idle
                ? 0
                : std::clamp(static_cast<int>(cycleTime / duration * clip.frameCount),
                             0, clip.frameCount - 1);
            animationApplied = UpdateRetargetedAnimation(
                baseCharacter_, animationSource_, clip, frame, baseCharacterBounds_,
                baseToAnimationSpace_, animationToBaseSpace_,
                animationSourceMeshIndices_, animationSourceVertexIndices_);
            if (!animationApplied) {
                animationRetargetingDisabled_ = true;
                TraceLog(LOG_WARNING,
                         "R6 animation produced an invalid pose; keeping the character in its bind pose.");
            }
        }
    }

    rlPushMatrix();
    rlTranslatef(feetPosition.x, feetPosition.y, feetPosition.z);
    // The Blender rig's authored front is +Z; the game camera at yaw 0 faces -Z.
    rlRotatef((PI - facingYaw) * RAD2DEG, 0.0f, 1.0f, 0.0f);
    const Vector3 modelOrigin{
        -(baseCharacterBounds_.min.x + baseCharacterBounds_.max.x) * 0.5f * baseCharacterScale_,
        -baseCharacterBounds_.min.y * baseCharacterScale_,
        -(baseCharacterBounds_.min.z + baseCharacterBounds_.max.z) * 0.5f * baseCharacterScale_};
    DrawModelEx(baseCharacter_, modelOrigin, Vector3{0.0f, 1.0f, 0.0f}, 0.0f,
                Vector3{baseCharacterScale_, baseCharacterScale_, baseCharacterScale_}, WHITE);
    const float modelCenterZ = (baseCharacterBounds_.min.z + baseCharacterBounds_.max.z) * 0.5f;
    const float faceFrontZ =
        (baseCharacterBounds_.max.z - modelCenterZ) * baseCharacterScale_ + 0.002f;
    Matrix faceTransform{};
    const Matrix *faceTransformPointer = nullptr;
    if (animationApplied && headMeshIndex_ >= 0 && headMeshIndex_ < baseCharacter_.meshCount &&
        static_cast<std::size_t>(headMeshIndex_) < baseToAnimationSpace_.size() &&
        static_cast<std::size_t>(headMeshIndex_) < animationToBaseSpace_.size() &&
        static_cast<std::size_t>(headMeshIndex_) < animationSourceMeshIndices_.size()) {
        const int sourceMeshIndex =
            animationSourceMeshIndices_[static_cast<std::size_t>(headMeshIndex_)];
        if (sourceMeshIndex >= 0 && sourceMeshIndex < animationSource_.meshCount &&
            animationSource_.meshes[sourceMeshIndex].boneMatrices) {
            int headBoneIndex = -1;
            for (int boneIndex = 0; boneIndex < animationSource_.boneCount; ++boneIndex) {
                if (std::strcmp(animationSource_.bones[boneIndex].name, "Head_01_7") == 0) {
                    headBoneIndex = boneIndex;
                    break;
                }
            }
            if (headBoneIndex >= 0) {
                const Matrix &toAnimation =
                    baseToAnimationSpace_[static_cast<std::size_t>(headMeshIndex_)];
                const Matrix &fromAnimation =
                    animationToBaseSpace_[static_cast<std::size_t>(headMeshIndex_)];
                faceTransform = MatrixMultiply(
                    MatrixMultiply(fromAnimation,
                                   animationSource_.meshes[sourceMeshIndex].boneMatrices[headBoneIndex]),
                    toAnimation);
                faceTransformPointer = &faceTransform;
            }
        }
    }
    Draw(avatar, false, faceFrontZ, faceTransformPointer);
    rlPopMatrix();
    return true;
}

AvatarAssetCache::TextureEntry& AvatarAssetCache::texture(const std::string& url) {
    auto& entry = textures_[url];
    if (entry.texture.id || (entry.attempted && GetTime() < entry.retryAt)) return entry;
    entry.attempted = true;
    entry.retryAt = GetTime() + 10.0;
#if defined(_WIN32)
    std::vector<unsigned char> bytes;
    if (!DownloadAsset(url, kMaxTextureBytes, bytes)) {
        TraceLog(LOG_WARNING, "Could not download an avatar clothing texture.");
        return entry;
    }
    const std::string fileType = ImageFileType(url, bytes);
    Image source = LoadImageFromMemory(fileType.c_str(), bytes.data(), static_cast<int>(bytes.size()));
    if (!source.data || source.width != 585 || source.height != 559) {
        if (source.data) UnloadImage(source);
        TraceLog(LOG_WARNING, "Avatar clothing asset was not a valid 585x559 PNG template.");
        return entry;
    }
    entry.texture = LoadTextureFromImage(source);
    UnloadImage(source);
    entry.retryAt = 0.0;
#endif
    return entry;
}

AvatarAssetCache::TextureEntry& AvatarAssetCache::itemTexture(const std::string& url) {
    auto& entry = textures_[url];
    if (entry.texture.id || (entry.attempted && GetTime() < entry.retryAt)) return entry;
    entry.attempted = true;
    entry.retryAt = GetTime() + 10.0;
#if defined(_WIN32)
    std::vector<unsigned char> bytes;
    if (!DownloadAsset(url, kMaxTextureBytes, bytes)) {
        TraceLog(LOG_WARNING, "Could not download a marketplace avatar texture.");
        return entry;
    }
    const std::string fileType = ImageFileType(url, bytes);
    Image source = LoadImageFromMemory(fileType.c_str(), bytes.data(), static_cast<int>(bytes.size()));
    if (!source.data || source.width <= 0 || source.height <= 0) {
        if (source.data) UnloadImage(source);
        TraceLog(LOG_WARNING, "Marketplace avatar texture could not be decoded.");
        return entry;
    }
    entry.texture = LoadTextureFromImage(source);
    UnloadImage(source);
    entry.retryAt = 0.0;
#endif
    return entry;
}

AvatarAssetCache::ModelEntry& AvatarAssetCache::model(const std::string& url, const std::string& textureUrl) {
    const std::string modelKey = url + "\n" + textureUrl;
    auto& entry = models_[modelKey];
    if (entry.attempted) return entry;
    entry.attempted = true;
#if defined(_WIN32)
    std::vector<unsigned char> bytes;
    if (!DownloadAsset(url, kMaxModelBytes, bytes) || bytes.size() < 12 ||
        bytes[0] != 'g' || bytes[1] != 'l' || bytes[2] != 'T' || bytes[3] != 'F') return entry;
    wchar_t tempRoot[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempRoot)) return entry;
    const auto fileHash = std::hash<std::string>{}(modelKey);
    const std::filesystem::path path = std::filesystem::path(tempRoot) /
        (L"blubox-avatar-" + std::to_wstring(fileHash) + L".glb");
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return entry;
    }
    entry.model = LoadModel(path.string().c_str());
    if (entry.model.meshCount > 0) {
        entry.bounds = GetModelBoundingBox(entry.model);
        // DrawModelEx selects the shader stored on each Model material. The
        // outer world BeginShaderMode does not override those material shaders,
        // so without this assignment uploaded accessories stayed unlit.
        if (lightingShader_.id != 0 && entry.model.materials) {
            for (int i = 0; i < entry.model.materialCount; ++i)
                entry.model.materials[i].shader = lightingShader_;
        }
    }
    if (entry.model.meshCount > 0 && !textureUrl.empty()) {
        const Texture2D itemMap = itemTexture(textureUrl).texture;
        if (itemMap.id) {
            for (int i = 0; i < entry.model.materialCount; ++i)
                SetMaterialTexture(&entry.model.materials[i], MATERIAL_MAP_DIFFUSE, itemMap);
            entry.hasExternalTexture = true;
        }
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
#endif
    return entry;
}

void AvatarAssetCache::Draw(const NetworkPlayerSnapshot& avatar, bool drawClothingOverlays,
                            float faceFrontZ, const Matrix *faceTransform) {
    Texture2D shirtTexture{};
    Texture2D pantsTexture{};
    Texture2D faceTexture{};
    if (drawClothingOverlays && !avatar.shirtAssetUrl.empty()) {
        shirtTexture = texture(avatar.shirtAssetUrl).texture;
    }
    if (drawClothingOverlays && !avatar.pantsAssetUrl.empty()) {
        pantsTexture = texture(avatar.pantsAssetUrl).texture;
    }
    if (!avatar.faceAssetUrl.empty()) {
        faceTexture = itemTexture(avatar.faceAssetUrl).texture;
    }

    if (shirtTexture.id != 0) {
        DrawOverlay(shirtTexture, Rectangle{231, 128, 116, 128}, 0.0f, 1.25f, 0.76f, 0.88f, 0.214f, ParseColor(avatar.shirtColor));
    }
    if (pantsTexture.id != 0) {
        DrawOverlay(pantsTexture, Rectangle{231, 256, 58, 128}, -0.23f, 0.43f, 0.31f, 0.72f, 0.171f, ParseColor(avatar.pantsColor));
        DrawOverlay(pantsTexture, Rectangle{347, 256, 58, 128}, 0.23f, 0.43f, 0.31f, 0.72f, 0.171f, ParseColor(avatar.pantsColor));
    }
    if (faceTexture.id != 0) {
        if (faceTransform) {
            rlPushMatrix();
            rlMultMatrixf(MatrixToFloat(*faceTransform));
        }
        DrawCurvedOverlay(faceTexture, 0.0f, 1.93f, 0.35f, 0.35f, faceFrontZ, WHITE);
        if (faceTransform) rlPopMatrix();
    }
    for (const auto& accessory : avatar.accessoryAssets) {
        ModelEntry& entry = model(accessory.assetUrl, accessory.textureAssetUrl);
        Model& asset = entry.model;
        if (!asset.meshCount) continue;

        // These sockets match AvatarPreview.fitAccessory() and its feet-origin
        // rig. Keep offsets in avatar-local space so the saved editor fit is
        // identical in the published client and follows the character yaw.
        const AccessoryFit fit = FitAccessory(entry.bounds, accessory);
        if (fit.scale <= 0.0f) continue;
        DrawModelEx(asset, fit.position, Vector3{0.0f, 1.0f, 0.0f}, 0.0f,
                    Vector3{fit.scale, fit.scale, fit.scale}, WHITE);
    }
}

void AvatarAssetCache::DrawGroundShadow(Vector3 groundPoint, Vector3 feetPosition,
                                        const NetworkPlayerSnapshot& avatar, float facingYaw,
                                        Vector3 sunToLight) {
    if (!shadowTexture_.id) {
        Image image = GenImageColor(64, 64, BLANK);
        if (!image.data) return;
        auto* pixels = static_cast<Color*>(image.data);
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                const float dx = (x + 0.5f - image.width * 0.5f) / (image.width * 0.5f);
                const float dy = (y + 0.5f - image.height * 0.5f) / (image.height * 0.5f);
                const float edge = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
                const auto alpha = static_cast<unsigned char>(std::lround(190.0f * edge * edge));
                pixels[y * image.width + x] = Color{12, 17, 25, alpha};
            }
        }
        shadowTexture_ = LoadTextureFromImage(image);
        UnloadImage(image);
        if (shadowTexture_.id) SetTextureFilter(shadowTexture_, TEXTURE_FILTER_BILINEAR);
    }
    if (!shadowTexture_.id) return;

    const float lightY = std::max(0.2f, std::abs(sunToLight.y));
    const float direction = sunToLight.y >= 0.0f ? -1.0f : 1.0f;
    const float projectX = direction * sunToLight.x / lightY;
    const float projectZ = direction * sunToLight.z / lightY;
    const float bodyHeight = std::max(0.0f, feetPosition.y + 0.85f - groundPoint.y);
    Vector3 bodyCenter{feetPosition.x + projectX * bodyHeight * 0.45f,
                       groundPoint.y,
                       feetPosition.z + projectZ * bodyHeight * 0.45f};
    const float bodyYaw = PI - facingYaw;
    DrawShadowQuad(shadowTexture_, groundPoint, bodyCenter, 0.39f, 0.25f,
                   bodyYaw, 112);

    // Each fitted accessory gets its own soft projected footprint. Reuse the
    // exact fit transform used for rendering so the shadow follows saved size,
    // attachment offsets, and character facing.
    for (const auto& accessory : avatar.accessoryAssets) {
        const auto it = models_.find(accessory.assetUrl + "\n" + accessory.textureAssetUrl);
        if (it == models_.end() || it->second.model.meshCount <= 0) continue;
        const AccessoryFit fit = FitAccessory(it->second.bounds, accessory);
        if (fit.scale <= 0.0f) continue;
        const Vector3 localCenter = RotateAvatarLocal(fit.center, facingYaw);
        const float height = std::max(0.0f, feetPosition.y + localCenter.y - groundPoint.y);
        Vector3 projected{feetPosition.x + localCenter.x + projectX * height,
                          groundPoint.y,
                          feetPosition.z + localCenter.z + projectZ * height};
        const Vector3 half = fit.halfSize;
        const float radiusX = std::clamp(std::abs(half.x) + std::abs(half.z), 0.10f, 0.75f);
        const float radiusZ = std::clamp(std::abs(half.z) + std::abs(half.x) * 0.45f, 0.09f, 0.60f);
        DrawShadowQuad(shadowTexture_, groundPoint, projected, radiusX, radiusZ,
                       bodyYaw, 120);
    }
}

void AvatarAssetCache::Unload() noexcept {
    if (baseCharacter_.meshCount > 0) UnloadModel(baseCharacter_);
    baseCharacter_ = Model{};
    baseCharacterMaterials_.clear();
    if (animationSource_.meshCount > 0) UnloadModel(animationSource_);
    animationSource_ = Model{};
    if (walkAnimations_) UnloadModelAnimations(walkAnimations_, walkAnimationCount_);
    walkAnimations_ = nullptr;
    walkAnimationCount_ = 0;
    if (jumpAnimations_) UnloadModelAnimations(jumpAnimations_, jumpAnimationCount_);
    jumpAnimations_ = nullptr;
    jumpAnimationCount_ = 0;
    baseToAnimationSpace_.clear();
    animationToBaseSpace_.clear();
    animationSourceMeshIndices_.clear();
    animationSourceVertexIndices_.clear();
    headMeshIndex_ = -1;
    baseCharacterLoaded_ = false;
    animationRetargetingDisabled_ = false;
    for (auto& [_, entry] : textures_) if (entry.texture.id) UnloadTexture(entry.texture);
    for (auto& [_, entry] : models_) {
        if (!entry.model.meshCount) continue;
        // External textures are owned by the shared texture cache, not by the model.
        if (entry.hasExternalTexture)
            for (int i = 0; i < entry.model.materialCount; ++i)
                entry.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture.id = 0;
        UnloadModel(entry.model);
    }
    textures_.clear();
    models_.clear();
    if (shadowTexture_.id) UnloadTexture(shadowTexture_);
    shadowTexture_ = Texture2D{};
    lightingShader_ = Shader{};
}
