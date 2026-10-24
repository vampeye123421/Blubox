#pragma once

#include <raylib.h>

#include <limits>
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class PublishedWorld {
public:
    struct StaticPart {
        std::string kind;
        Vector3 position{};
        Vector3 size{1.0f, 1.0f, 1.0f};
        Color color{75, 163, 226, 255};
        bool visible = true;
        bool collidable = true;
    };

    struct LocalScript {
        std::string id;
        std::string name;
        std::string parent;
        std::string source;
    };

    bool Load(std::string_view worldId, std::string &error);
    const std::vector<StaticPart> &Parts() const noexcept { return parts_; }
    Vector3 SpawnPosition() const noexcept { return spawnPosition_; }
    bool HasParts() const noexcept { return !parts_.empty(); }
    bool HasGui() const noexcept { return !gui_.empty(); }
    const std::vector<LocalScript> &LocalScripts() const noexcept { return localScripts_; }
    void DrawGui(int screenWidth, int screenHeight, const Font *font) const;
    // Returns true if the pointer gesture belongs to a UI button and must not
    // continue into camera/world input. Activated is fired on release over the
    // originally pressed button (pointer capture semantics).
    bool HandleGuiPointer(Vector2 pointer, bool pressed, bool released, std::string &activatedId);
    std::optional<std::string> GuiParent(std::string_view id) const;
    std::optional<std::string> GuiName(std::string_view id) const;
    std::optional<std::string> GuiText(std::string_view id) const;
    std::optional<bool> GuiVisible(std::string_view id) const;
    bool SetGuiParent(std::string_view id, std::string_view parentId);
    bool SetGuiName(std::string_view id, std::string_view value);
    bool SetGuiText(std::string_view id, std::string_view value);
    bool SetGuiVisible(std::string_view id, bool value);
    bool SetGuiTransparency(std::string_view id, std::string_view property, float value);
    std::optional<float> GuiTransparency(std::string_view id, std::string_view property) const;
    bool SetGuiColor(std::string_view id, std::string_view property, Color value);
    std::optional<Color> GuiColor(std::string_view id, std::string_view property) const;
    bool SetGuiLayout(std::string_view id, std::string_view property,
                      Vector2 offset, Vector2 scale);
    bool GetGuiLayout(std::string_view id, std::string_view property,
                      Vector2 &offset, Vector2 &scale) const;
    std::optional<std::string> FindGuiChild(std::string_view parentId, std::string_view childName) const;
    std::string CreateGui(std::string_view className);

private:
    struct GuiElement {
        std::string id;
        std::string name;
        std::string parent;
        std::size_t parentIndex = std::numeric_limits<std::size_t>::max();
        std::string kind;
        std::string text;
        std::string horizontalAlignment;
        std::string verticalAlignment;
        std::string textXAlignment;
        std::string textYAlignment;
        Vector2 position{};
        Vector2 positionScale{};
        Vector2 size{220.0f, 64.0f};
        Vector2 sizeScale{};
        Color background{34, 44, 58, 255};
        Color textColor{242, 246, 252, 255};
        float backgroundTransparency = 0.0f;
        float textTransparency = 0.0f;
        float fontSize = 20.0f;
        float cornerRadius = 0.0f;
        bool scaleWithViewport = true;
        bool textScaled = false;
        bool textWrapped = false;
        bool textTruncate = false;
        bool visible = true;
        bool enabled = true;
        int zIndex = 1;
        std::vector<std::size_t> children;
    };

    struct Rect { float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f; };
    Rect ResolveGuiRect(std::size_t index, float width, float height, std::size_t depth) const;
    void DrawGuiElement(std::size_t index, int screenWidth, int screenHeight,
                        const Font *font, std::size_t depth) const;
    void DrawGuiChildren(std::size_t parentIndex, int screenWidth, int screenHeight,
                         const Font *font, std::size_t depth) const;

    std::vector<StaticPart> parts_;
    std::vector<GuiElement> gui_;
    std::unordered_map<std::string, std::size_t> guiIndex_;
    std::vector<LocalScript> localScripts_;
    std::vector<std::size_t> screenGuis_;
    std::optional<std::size_t> capturedButton_;
    Vector3 spawnPosition_{0.0f, 3.0f, 0.0f};
    std::uint64_t nextLocalGuiId_ = 1;
};
