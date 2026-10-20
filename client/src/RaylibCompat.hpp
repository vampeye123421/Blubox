#pragma once

#include <raylib.h>

// Avoid passing Camera3D by value from AVX-enabled MinGW client code.
void BeginMode3DSafe(const Camera3D *camera);
void DrawBillboardSafe(const Camera3D *camera, Texture2D texture, Vector3 position,
                      float scale, Color tint);
void DrawBillboardProSafe(const Camera3D *camera, Texture2D texture, Rectangle source,
                          Vector3 position, Vector3 up, Vector2 size, Vector2 origin,
                          float rotation, Color tint);
void CreateCubeModelSafe(Model *model, float width, float height, float length);
void CreateCapsuleCharacterModelsSafe(Model *cylinder, Model *sphere, float radius,
                                     float cylinderHeight, int slices, int rings);
void DrawModelSafe(const Model *model, const Vector3 *position, float scale, const Color *tint);
bool LoadFontExSafe(Font *font, const char *fileName, int fontSize);
bool LoadFontFromMemorySafe(Font *font, const char *fileType, const unsigned char *data,
                            int dataSize, int fontSize);
void GetFontDefaultSafe(Font *font);
Vector2 MeasureTextExSafe(const Font *font, const char *text, float fontSize, float spacing);
void DrawTextExSafe(const Font *font, const char *text, Vector2 position, float fontSize,
                    float spacing, Color tint);
Texture2D CreateNameplateTextureSafe(const Font *font, const char *text);
