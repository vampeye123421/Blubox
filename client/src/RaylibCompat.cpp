#include "RaylibCompat.hpp"

#include <raylib.h>

void BeginMode3DSafe(const Camera3D *camera) {
    if (camera != nullptr) BeginMode3D(*camera);
}

void DrawBillboardSafe(const Camera3D *camera, Texture2D texture, Vector3 position,
                       float scale, Color tint) {
    if (camera != nullptr) DrawBillboard(*camera, texture, position, scale, tint);
}

void DrawBillboardProSafe(const Camera3D *camera, Texture2D texture, Rectangle source,
                          Vector3 position, Vector3 up, Vector2 size, Vector2 origin,
                          float rotation, Color tint) {
    if (camera != nullptr) {
        DrawBillboardPro(*camera, texture, source, position, up, size, origin, rotation, tint);
    }
}

void CreateCubeModelSafe(Model *model, float width, float height, float length) {
    if (model == nullptr) return;
    *model = LoadModelFromMesh(GenMeshCube(width, height, length));
}

void CreateCapsuleCharacterModelsSafe(Model *cylinder, Model *sphere, float radius,
                                     float cylinderHeight, int slices, int rings) {
    if (cylinder == nullptr || sphere == nullptr) return;
    *cylinder = LoadModelFromMesh(GenMeshCylinder(radius, cylinderHeight, slices));
    *sphere = LoadModelFromMesh(GenMeshSphere(radius, rings, slices));
}

void DrawModelSafe(const Model *model, const Vector3 *position, float scale, const Color *tint) {
    if (model != nullptr && position != nullptr && tint != nullptr) {
        DrawModel(*model, *position, scale, *tint);
    }
}

bool LoadFontExSafe(Font *font, const char *fileName, int fontSize) {
    if (font == nullptr || fileName == nullptr) return false;
    *font = LoadFontEx(fileName, fontSize, nullptr, 0);
    return font->texture.id != 0;
}

bool LoadFontFromMemorySafe(Font *font, const char *fileType, const unsigned char *data,
                            int dataSize, int fontSize) {
    if (font == nullptr || fileType == nullptr || data == nullptr || dataSize <= 0) return false;
    *font = LoadFontFromMemory(fileType, data, dataSize, fontSize, nullptr, 0);
    return font->texture.id != 0;
}

void GetFontDefaultSafe(Font *font) {
    if (font != nullptr) *font = GetFontDefault();
}

Vector2 MeasureTextExSafe(const Font *font, const char *text, float fontSize, float spacing) {
    if (font == nullptr || text == nullptr) return Vector2{ 0.0f, 0.0f };
    return MeasureTextEx(*font, text, fontSize, spacing);
}

void DrawTextExSafe(const Font *font, const char *text, Vector2 position, float fontSize,
                    float spacing, Color tint) {
    if (font != nullptr && text != nullptr) {
        DrawTextEx(*font, text, position, fontSize, spacing, tint);
    }
}

Texture2D CreateNameplateTextureSafe(const Font *font, const char *text) {
    if (font == nullptr || text == nullptr || text[0] == '\0') return Texture2D{};
    constexpr float fontSize = 40.0f;
    constexpr float spacing = 1.0f;
    constexpr int paddingX = 18;
    constexpr int paddingY = 12;
    const Vector2 measured = MeasureTextEx(*font, text, fontSize, spacing);
    const int width = static_cast<int>(measured.x) + paddingX * 2;
    const int height = static_cast<int>(measured.y) + paddingY * 2;
    Image image = GenImageColor(width, height, BLANK);
    if (image.data == nullptr) return Texture2D{};
    ImageDrawTextEx(&image, *font, text, Vector2{static_cast<float>(paddingX + 1),
                                                 static_cast<float>(paddingY + 2)},
                    fontSize, spacing, Color{12, 22, 34, 190});
    ImageDrawTextEx(&image, *font, text, Vector2{static_cast<float>(paddingX),
                                                 static_cast<float>(paddingY)},
                    fontSize, spacing, Color{250, 253, 255, 255});
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id != 0) SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}
