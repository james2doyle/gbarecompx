// raylib_api.h — the raylib surface this backend calls, behind one interface.
//
// WHY THIS EXISTS
// ---------------
// The backend must be testable without a display and with injected failures
// (the Phase 5 fake: InitWindow fails -> Err::Window; shader load fails ->
// caps degrade; audio device unavailable -> claim() refuses; monitor/refresh
// edge cases). Every raylib call site in raylib_backend.cpp goes through
// `RaylibApi`, so the fake needs no backend changes.
//
// The methods mirror raylib's names and signatures one-to-one; the real
// implementation forwards (raylib_api.cpp). Time lives in `Clock`
// (host_clock.h) — the fake clock is a separate concern.
//
// This header includes raylib.h and is only compiled in
// `GBARECOMP_USE_RAYLIB` builds; the stub build calls no raylib.
#pragma once

#if defined(GBARECOMP_USE_RAYLIB)

#include <raylib.h>

namespace gbarecomp {

struct RaylibApi {
    virtual ~RaylibApi() = default;

    // ── window / monitor ────────────────────────────────────────────────
    virtual void InitWindow(int width, int height, const char* title) = 0;
    virtual void CloseWindow() = 0;
    virtual bool IsWindowReady() = 0;
    virtual void SetConfigFlags(unsigned int flags) = 0;
    virtual void SetWindowTitle(const char* title) = 0;
    virtual void SetWindowState(unsigned int flags) = 0;
    virtual void ClearWindowState(unsigned int flags) = 0;
    virtual void SetTargetFPS(int fps) = 0;
    virtual bool WindowShouldClose() = 0;
    virtual int GetScreenWidth() = 0;
    virtual int GetScreenHeight() = 0;
    virtual int GetCurrentMonitor() = 0;
    virtual int GetMonitorRefreshRate(int monitor) = 0;
    virtual int GetMonitorCount() = 0;

    // ── textures / render ───────────────────────────────────────────────
    virtual Texture2D LoadTextureFromImage(Image image) = 0;
    virtual void UnloadTexture(Texture2D texture) = 0;
    virtual void UpdateTexture(Texture2D texture, const void* pixels) = 0;
    virtual void SetTextureFilter(Texture2D texture, int filter) = 0;
    virtual RenderTexture2D LoadRenderTexture(int width, int height) = 0;
    virtual void UnloadRenderTexture(RenderTexture2D target) = 0;
    virtual void BeginTextureMode(RenderTexture2D target) = 0;
    virtual void EndTextureMode() = 0;
    virtual void DrawTexturePro(Texture2D texture, Rectangle source,
                                Rectangle dest, Vector2 origin, float rotation,
                                Color tint) = 0;
    virtual void ClearBackground(Color color) = 0;
    virtual void BeginDrawing() = 0;
    virtual void EndDrawing() = 0;
    virtual Color GetColor(unsigned int hexValue) = 0;

    // ── shaders ─────────────────────────────────────────────────────────
    virtual Shader LoadShaderFromMemory(const char* vsCode,
                                        const char* fsCode) = 0;
    virtual void UnloadShader(Shader shader) = 0;
    virtual int GetShaderLocation(Shader shader, const char* uniformName) = 0;
    virtual void SetShaderValue(Shader shader, int locIndex, const void* value,
                                int uniformType) = 0;
    virtual void BeginShaderMode(Shader shader) = 0;
    virtual void EndShaderMode() = 0;
    virtual bool IsShaderValid(Shader shader) = 0;

    // ── audio ───────────────────────────────────────────────────────────
    virtual void InitAudioDevice() = 0;
    virtual void CloseAudioDevice() = 0;
    virtual bool IsAudioDeviceReady() = 0;
    virtual AudioStream LoadAudioStream(unsigned int sampleRate,
                                        unsigned int sampleSize,
                                        unsigned int channels) = 0;
    virtual void UnloadAudioStream(AudioStream stream) = 0;
    virtual void UpdateAudioStream(AudioStream stream, const void* data,
                                   int frameCount) = 0;
    virtual void PlayAudioStream(AudioStream stream) = 0;
    virtual void StopAudioStream(AudioStream stream) = 0;
    virtual void SetAudioStreamCallback(AudioStream stream,
                                        AudioCallback callback) = 0;
    virtual void SetAudioStreamBufferSizeDefault(int size) = 0;
    virtual bool IsAudioStreamValid(AudioStream stream) = 0;

    // ── input ───────────────────────────────────────────────────────────
    virtual bool IsKeyDown(int key) = 0;
    virtual bool IsKeyPressed(int key) = 0;
    virtual bool IsMouseButtonPressed(int button) = 0;
    virtual bool IsMouseButtonDown(int button) = 0;
    virtual int GetMouseX() = 0;
    virtual int GetMouseY() = 0;
    virtual Vector2 GetMouseDelta() = 0;
    virtual Vector2 GetTouchPosition(int index) = 0;
    virtual int GetTouchPointCount() = 0;
    virtual int GetTouchPointId(int index) = 0;
    virtual bool IsGamepadAvailable(int gamepad) = 0;
    virtual float GetGamepadAxisMovement(int gamepad, int axis) = 0;
    virtual bool IsGamepadButtonDown(int gamepad, int button) = 0;
    virtual void SetGamepadVibration(int gamepad, float leftMotor,
                                     float rightMotor, float duration) = 0;

    // ── overlay drawing (screen space) ──────────────────────────────────
    virtual void DrawLineEx(Vector2 startPos, Vector2 endPos, float thick,
                            Color color) = 0;
    virtual void DrawRing(Vector2 center, float innerRadius, float outerRadius,
                          float startAngle, float endAngle, int segments,
                          Color color) = 0;
    virtual void DrawCircleV(Vector2 center, float radius, Color color) = 0;
    virtual void DrawRectangleV(Vector2 position, Vector2 size,
                                Color color) = 0;
    virtual void DrawRectangleRounded(Rectangle rec, float roundness,
                                      int segments, Color color) = 0;
    virtual void DrawRectangleRoundedLines(Rectangle rec, float roundness,
                                           int segments, Color color) = 0;
    virtual void DrawRectangleLinesEx(Rectangle rec, float lineThick,
                                      Color color) = 0;
};

// The real raylib-backed instance (process-wide; stateless forwarding).
RaylibApi& real_raylib_api();

}  // namespace gbarecomp

#endif  // defined(GBARECOMP_USE_RAYLIB)
