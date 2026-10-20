#include "RaylibCompat.hpp"

#include <raylib.h>

void BeginMode3DSafe(const Camera3D *camera) {
    if (camera != nullptr) BeginMode3D(*camera);
}

void DrawBillboardSafe(const Camera3D *camera, Texture2D texture, Vector3 position,
                       float scale, Color tint) {
    if (camera != nullptr) DrawBillboard(*camera, texture, position, scale, tint);
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
