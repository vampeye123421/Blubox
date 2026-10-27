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
    bool LoadBaseCharacter(const Shader& lightingShader);
    bool DrawBaseCharacter(Vector3 feetPosition, const NetworkPlayerSnapshot& avatar,
                           float facingYaw);
    void Draw(const NetworkPlayerSnapshot& avatar, bool drawClothingOverlays = true,
              float faceFrontZ = 0.325f);
    void DrawGroundShadow(Vector3 groundPoint, Vector3 feetPosition,
                          const NetworkPlayerSnapshot& avatar, float facingYaw,
                          Vector3 sunToLight);
    void Unload() noexcept;

private:
    struct TextureEntry { Texture2D texture{}; bool attempted = false; double retryAt = 0.0; };
    struct ModelEntry { Model model{}; BoundingBox bounds{}; bool attempted = false; bool hasExternalTexture = false; };
    Model baseCharacter_{};
    BoundingBox baseCharacterBounds_{};
    std::unordered_map<std::string, int> baseCharacterMaterials_;
    // The client owns this shader; cached accessories only borrow the handle.
    // Assigning it per material is required because DrawModelEx uses each
    // material's shader instead of inheriting BeginShaderMode's active shader.
    Shader lightingShader_{};
    Texture2D shadowTexture_{};
    float baseCharacterScale_ = 1.0f;
    bool baseCharacterLoaded_ = false;
    TextureEntry& texture(const std::string& url);
    TextureEntry& itemTexture(const std::string& url);
    ModelEntry& model(const std::string& url, const std::string& textureUrl);
    std::unordered_map<std::string, TextureEntry> textures_;
    std::unordered_map<std::string, ModelEntry> models_;
};
