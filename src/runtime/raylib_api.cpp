// raylib_api.cpp — the real RaylibApi: one-to-one forwarding to raylib.
//
// Every method is `::`-qualified: inside a member function an unqualified
// raylib name would resolve to the member and recurse.
#include "raylib_api.h"

#if defined(GBARECOMP_USE_RAYLIB)

namespace gbarecomp {
namespace {

struct RealRaylibApi final : RaylibApi {
    void InitWindow(int width, int height, const char* title) override {
        ::InitWindow(width, height, title);
    }
    void CloseWindow() override { ::CloseWindow(); }
    bool IsWindowReady() override { return ::IsWindowReady(); }
    void SetConfigFlags(unsigned int flags) override {
        ::SetConfigFlags(flags);
    }
    void SetWindowTitle(const char* title) override {
        ::SetWindowTitle(title);
    }
    void SetWindowState(unsigned int flags) override {
        ::SetWindowState(flags);
    }
    void ClearWindowState(unsigned int flags) override {
        ::ClearWindowState(flags);
    }
    void SetTargetFPS(int fps) override { ::SetTargetFPS(fps); }
    bool WindowShouldClose() override { return ::WindowShouldClose(); }
    int GetScreenWidth() override { return ::GetScreenWidth(); }
    int GetScreenHeight() override { return ::GetScreenHeight(); }
    int GetCurrentMonitor() override { return ::GetCurrentMonitor(); }
    int GetMonitorRefreshRate(int monitor) override {
        return ::GetMonitorRefreshRate(monitor);
    }
    int GetMonitorCount() override { return ::GetMonitorCount(); }

    Texture2D LoadTextureFromImage(Image image) override {
        return ::LoadTextureFromImage(image);
    }
    void UnloadTexture(Texture2D texture) override {
        ::UnloadTexture(texture);
    }
    void UpdateTexture(Texture2D texture, const void* pixels) override {
        ::UpdateTexture(texture, pixels);
    }
    void SetTextureFilter(Texture2D texture, int filter) override {
        ::SetTextureFilter(texture, filter);
    }
    RenderTexture2D LoadRenderTexture(int width, int height) override {
        return ::LoadRenderTexture(width, height);
    }
    void UnloadRenderTexture(RenderTexture2D target) override {
        ::UnloadRenderTexture(target);
    }
    void BeginTextureMode(RenderTexture2D target) override {
        ::BeginTextureMode(target);
    }
    void EndTextureMode() override { ::EndTextureMode(); }
    void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest,
                        Vector2 origin, float rotation, Color tint) override {
        ::DrawTexturePro(texture, source, dest, origin, rotation, tint);
    }
    void ClearBackground(Color color) override {
        ::ClearBackground(color);
    }
    void BeginDrawing() override { ::BeginDrawing(); }
    void EndDrawing() override { ::EndDrawing(); }
    Color GetColor(unsigned int hexValue) override {
        return ::GetColor(hexValue);
    }
    void DrawText(const char* text, int posX, int posY, int fontSize,
                  Color color) override {
        ::DrawText(text, posX, posY, fontSize, color);
    }
    int MeasureText(const char* text, int fontSize) override {
        return ::MeasureText(text, fontSize);
    }

    Shader LoadShaderFromMemory(const char* vsCode,
                                const char* fsCode) override {
        return ::LoadShaderFromMemory(vsCode, fsCode);
    }
    void UnloadShader(Shader shader) override { ::UnloadShader(shader); }
    int GetShaderLocation(Shader shader, const char* uniformName) override {
        return ::GetShaderLocation(shader, uniformName);
    }
    void SetShaderValue(Shader shader, int locIndex, const void* value,
                        int uniformType) override {
        ::SetShaderValue(shader, locIndex, value, uniformType);
    }
    void BeginShaderMode(Shader shader) override {
        ::BeginShaderMode(shader);
    }
    void EndShaderMode() override { ::EndShaderMode(); }
    bool IsShaderValid(Shader shader) override {
        return ::IsShaderValid(shader);
    }

    void InitAudioDevice() override { ::InitAudioDevice(); }
    void CloseAudioDevice() override { ::CloseAudioDevice(); }
    bool IsAudioDeviceReady() override { return ::IsAudioDeviceReady(); }
    AudioStream LoadAudioStream(unsigned int sampleRate,
                                unsigned int sampleSize,
                                unsigned int channels) override {
        return ::LoadAudioStream(sampleRate, sampleSize, channels);
    }
    void UnloadAudioStream(AudioStream stream) override {
        ::UnloadAudioStream(stream);
    }
    void UpdateAudioStream(AudioStream stream, const void* data,
                           int frameCount) override {
        ::UpdateAudioStream(stream, data, frameCount);
    }
    void PlayAudioStream(AudioStream stream) override {
        ::PlayAudioStream(stream);
    }
    void StopAudioStream(AudioStream stream) override {
        ::StopAudioStream(stream);
    }
    void SetAudioStreamCallback(AudioStream stream,
                                AudioCallback callback) override {
        ::SetAudioStreamCallback(stream, callback);
    }
    void SetAudioStreamBufferSizeDefault(int size) override {
        ::SetAudioStreamBufferSizeDefault(size);
    }
    bool IsAudioStreamValid(AudioStream stream) override {
        return ::IsAudioStreamValid(stream);
    }

    bool IsKeyDown(int key) override { return ::IsKeyDown(key); }
    bool IsKeyPressed(int key) override { return ::IsKeyPressed(key); }
    bool IsMouseButtonPressed(int button) override {
        return ::IsMouseButtonPressed(button);
    }
    bool IsMouseButtonDown(int button) override {
        return ::IsMouseButtonDown(button);
    }
    int GetMouseX() override { return ::GetMouseX(); }
    int GetMouseY() override { return ::GetMouseY(); }
    Vector2 GetMouseDelta() override { return ::GetMouseDelta(); }
    Vector2 GetTouchPosition(int index) override {
        return ::GetTouchPosition(index);
    }
    int GetTouchPointCount() override { return ::GetTouchPointCount(); }
    int GetTouchPointId(int index) override { return ::GetTouchPointId(index); }
    bool IsGamepadAvailable(int gamepad) override {
        return ::IsGamepadAvailable(gamepad);
    }
    float GetGamepadAxisMovement(int gamepad, int axis) override {
        return ::GetGamepadAxisMovement(gamepad, axis);
    }
    bool IsGamepadButtonDown(int gamepad, int button) override {
        return ::IsGamepadButtonDown(gamepad, button);
    }
    void SetGamepadVibration(int gamepad, float leftMotor, float rightMotor,
                             float duration) override {
        ::SetGamepadVibration(gamepad, leftMotor, rightMotor, duration);
    }

    void DrawLineEx(Vector2 startPos, Vector2 endPos, float thick,
                    Color color) override {
        ::DrawLineEx(startPos, endPos, thick, color);
    }
    void DrawRing(Vector2 center, float innerRadius, float outerRadius,
                  float startAngle, float endAngle, int segments,
                  Color color) override {
        ::DrawRing(center, innerRadius, outerRadius, startAngle, endAngle,
                   segments, color);
    }
    void DrawCircleV(Vector2 center, float radius, Color color) override {
        ::DrawCircleV(center, radius, color);
    }
    void DrawRectangleV(Vector2 position, Vector2 size,
                        Color color) override {
        ::DrawRectangleV(position, size, color);
    }
    void DrawRectangleRounded(Rectangle rec, float roundness, int segments,
                              Color color) override {
        ::DrawRectangleRounded(rec, roundness, segments, color);
    }
    void DrawRectangleRoundedLines(Rectangle rec, float roundness, int segments,
                                   Color color) override {
        ::DrawRectangleRoundedLines(rec, roundness, segments, color);
    }
    void DrawRectangleLinesEx(Rectangle rec, float lineThick,
                              Color color) override {
        ::DrawRectangleLinesEx(rec, lineThick, color);
    }
};

}  // namespace

RaylibApi& real_raylib_api() {
    static RealRaylibApi api;
    return api;
}

}  // namespace gbarecomp

#endif  // defined(GBARECOMP_USE_RAYLIB)
