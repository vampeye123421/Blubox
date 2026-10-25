#pragma once

#include "NetworkClient.hpp"
#include <raylib.h>
#include <string>
#include <unordered_map>

// GPU resources are owned by the client and keyed by the signed marketplace
// asset URL. Network snapshots only carry immutable metadata.
class AvatarAssetCache {
public:
    ~AvatarAssetCache();
    void Draw(const NetworkPlayerSnapshot& avatar);
    void Unload() noexcept;

private:
    struct TextureEntry { Texture2D texture{}; bool attempted = false; };
    struct ModelEntry { Model model{}; bool attempted = false; };
    TextureEntry& texture(const std::string& url);
    ModelEntry& model(const std::string& url);
    std::unordered_map<std::string, TextureEntry> textures_;
    std::unordered_map<std::string, ModelEntry> models_;
};
