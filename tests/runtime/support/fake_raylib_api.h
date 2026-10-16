// fake_raylib_api.h — Phase 5.1, §5.2 item 4: failure injection for the real
// RaylibBackend with no display, no GPU, no devices.
//
// Scriptable knobs (public state; defaults let open() succeed):
//   window_ready      IsWindowReady after InitWindow (false -> Err::Window)
//   texture_id        LoadTextureFromImage id (0 -> Err::Renderer)
//   shader_valid      IsShaderValid (false -> soft fail, values still stored)
//   audio_ready       IsAudioDeviceReady (false -> claim() refuses)
//   audio_stream_ok   IsAudioStreamValid
//   monitor_count     GetMonitorCount
//   refresh_hz        GetMonitorRefreshRate (0 -> 60 Hz fallback)
//   key_down_held / key_pressed_held   IsKeyDown/IsKeyPressed answer
//   press_queue_once  one-shot GetKeyPressed value (then 0)
//
// Stub config (GBARECOMP_USE_RAYLIB off): RaylibApi does not exist; this
// header is empty so the unconditional include still resolves.
#pragma once

#if defined(GBARECOMP_USE_RAYLIB)

#include "raylib_api.h"

#include <map>
#include <string>
#include <vector>

namespace gbarecomp::testing {

// One SetShaderValue call, recovered by name so a test can assert WHAT was
// uploaded rather than merely that something was.
struct ShaderUniformWrite {
    std::string name;
    int loc = -1;
    int type = 0;
    int components = 0;
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

class FakeRaylibApi final : public RaylibApi {
public:
    // ── window / monitor ──────────────────────────────────────────────
    void InitWindow(int width, int height, const char* title) override {
        ++init_window_calls;
        last_w = width;
        last_h = height;
        window_open = true;
        (void)title;
    }
    void CloseWindow() override { window_open = false; }
    bool IsWindowReady() override { return window_open && window_ready; }
    void SetConfigFlags(unsigned int flags) override { config_flags = flags; }
    void SetWindowTitle(const char* title) override {
        ++set_title_calls;
        (void)title;
    }
    void SetWindowSize(int width, int height) override {
        ++set_window_size_calls;
        last_window_w = width;
        last_window_h = height;
    }
    void SetWindowState(unsigned int flags) override {
        ++set_window_state_calls;
        last_set_state = flags;
    }
    void ClearWindowState(unsigned int flags) override {
        ++clear_window_state_calls;
        last_clear_state = flags;
    }
    void SetTargetFPS(int fps) override { target_fps = fps; }
    bool WindowShouldClose() override { return should_close; }
    int GetScreenWidth() override { return screen_w; }
    int GetScreenHeight() override { return screen_h; }
    int GetCurrentMonitor() override { return 0; }
    int GetMonitorRefreshRate(int monitor) override {
        (void)monitor;
        return refresh_hz;
    }
    int GetMonitorCount() override { return monitor_count; }

    // ── textures / render ─────────────────────────────────────────────
    Texture2D LoadTextureFromImage(Image image) override {
        (void)image;
        Texture2D t{};
        t.id = texture_id;
        return t;
    }
    void UnloadTexture(Texture2D texture) override { (void)texture; }
    void UpdateTexture(Texture2D texture, const void* pixels) override {
        ++update_texture_calls;
        (void)texture;
        (void)pixels;
    }
    void SetTextureFilter(Texture2D texture, int filter) override {
        ++set_texture_filter_calls;
        last_texture_filter = filter;
        (void)texture;
    }
    // Non-zero id by default: the backend treats id == 0 as "target creation
    // failed" and silently degrades the pass, which would hide the uniform
    // uploads a test is here to observe. render_texture_id = 0 models the
    // failure path.
    RenderTexture2D LoadRenderTexture(int width, int height) override {
        load_render_texture_calls++;
        last_rt_w = width;
        last_rt_h = height;
        RenderTexture2D rt{};
        rt.id = render_texture_id;
        rt.texture.id = render_texture_id ? 2 : 0;
        return rt;
    }
    void UnloadRenderTexture(RenderTexture2D target) override {
        (void)target;
    }
    void BeginTextureMode(RenderTexture2D target) override { (void)target; }
    void EndTextureMode() override {}
    void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest,
                        Vector2 origin, float rotation, Color tint) override {
        (void)texture;
        (void)source;
        (void)dest;
        (void)origin;
        (void)rotation;
        (void)tint;
    }
    void ClearBackground(Color color) override { (void)color; }
    void BeginDrawing() override {}
    void EndDrawing() override {}
    Color GetColor(unsigned int hexValue) override {
        (void)hexValue;
        return Color{0, 0, 0, 0};
    }
    void DrawText(const char* text, int posX, int posY, int fontSize,
                  Color color) override {
        (void)text;
        (void)posX;
        (void)posY;
        (void)fontSize;
        (void)color;
    }
    int MeasureText(const char* text, int fontSize) override {
        (void)text;
        (void)fontSize;
        return 0;
    }
    // ── shaders ───────────────────────────────────────────────────────
    // Locations are handed out per NAME so a test can assert a uniform was
    // uploaded with the right value -- the grid uniforms are exactly that kind
    // of claim ("uGrid == origin+scale of the current layout"), and a fake
    // that returned -1 for everything could never observe it.
    // unknown_shader_locations models a driver that refuses every lookup,
    // which is the case the backend's `loc >= 0` guards exist for.
    Shader LoadShaderFromMemory(const char* vsCode,
                                const char* fsCode) override {
        (void)vsCode;
        uniform_names.clear();
        shader_source = fsCode ? fsCode : nullptr;
        return Shader{shader_id, nullptr};
    }
    void UnloadShader(Shader shader) override { (void)shader; }
    int GetShaderLocation(Shader shader, const char* uniformName) override {
        (void)shader;
        if (!uniformName) return -1;
        if (unknown_shader_locations) return -1;
        auto it = uniform_names.find(uniformName);
        if (it != uniform_names.end()) return it->second;
        const int loc = next_uniform_location++;
        uniform_names[uniformName] = loc;
        return loc;
    }
    void SetShaderValue(Shader shader, int locIndex, const void* value,
                        int uniformType) override {
        (void)shader;
        uniform_writes++;
        if (locIndex < 0 || !value) return;
        // Component count by uniform type (raylib's SHADER_UNIFORM_*), so a
        // vec4 write is recorded whole and a float write does not over-read.
        int comps = 1;
        switch (uniformType) {
            case SHADER_UNIFORM_VEC2: comps = 2; break;
            case SHADER_UNIFORM_VEC3: comps = 3; break;
            case SHADER_UNIFORM_VEC4: comps = 4; break;
            default: comps = 1; break;
        }
        ShaderUniformWrite w{};
        w.loc = locIndex;
        w.type = uniformType;
        w.components = comps;
        const float* f = static_cast<const float*>(value);
        for (int i = 0; i < comps; ++i) w.values[i] = f[i];
        for (const auto& kv : uniform_names)
            if (kv.second == locIndex) w.name = kv.first;
        uniform_write_log.push_back(w);
    }
    // Last value written to a named uniform, or false if never written.
    bool uniform_value(const char* uniformName, float* out,
                       int components) const {
        for (auto it = uniform_write_log.rbegin();
             it != uniform_write_log.rend(); ++it) {
            if (it->name == uniformName && it->components == components) {
                for (int i = 0; i < components; ++i) out[i] = it->values[i];
                return true;
            }
        }
        return false;
    }
    bool uniform_written(const char* uniformName) const {
        for (const ShaderUniformWrite& w : uniform_write_log)
            if (w.name == uniformName) return true;
        return false;
    }
    void clear_uniform_writes() { uniform_write_log.clear(); }
    void BeginShaderMode(Shader shader) override { (void)shader; }
    void EndShaderMode() override {}
    bool IsShaderValid(Shader shader) override {
        (void)shader;
        return shader_valid;
    }

    // ── audio ─────────────────────────────────────────────────────────
    void InitAudioDevice() override {}
    void CloseAudioDevice() override {}
    bool IsAudioDeviceReady() override { return audio_ready; }
    AudioStream LoadAudioStream(unsigned int sampleRate,
                                unsigned int sampleSize,
                                unsigned int channels) override {
        AudioStream s{};
        s.sampleRate = sampleRate;
        s.sampleSize = sampleSize;
        s.channels = channels;
        return s;
    }
    void UnloadAudioStream(AudioStream stream) override { (void)stream; }
    void UpdateAudioStream(AudioStream stream, const void* data,
                           int frameCount) override {
        (void)stream;
        (void)data;
        (void)frameCount;
    }
    void PlayAudioStream(AudioStream stream) override { (void)stream; }
    void StopAudioStream(AudioStream stream) override { (void)stream; }
    void SetAudioStreamCallback(AudioStream stream, AudioCallback callback) override {
        (void)stream;
        (void)callback;
    }
    void SetAudioStreamBufferSizeDefault(int size) override { (void)size; }
    bool IsAudioStreamValid(AudioStream stream) override {
        (void)stream;
        return audio_stream_ok;
    }

    // ── input ─────────────────────────────────────────────────────────
    bool IsKeyDown(int key) override {
        (void)key;
        return key_down_held;
    }
    bool IsKeyPressed(int key) override {
        (void)key;
        return key_pressed_held;
    }
    int GetKeyPressed() override {
        const int v = press_queue_once;
        press_queue_once = 0;
        return v;
    }
    bool IsMouseButtonPressed(int button) override {
        (void)button;
        return false;
    }
    bool IsMouseButtonDown(int button) override {
        (void)button;
        return false;
    }
    int GetMouseX() override { return 0; }
    int GetMouseY() override { return 0; }
    Vector2 GetMouseDelta() override { return Vector2{0.0f, 0.0f}; }
    Vector2 GetTouchPosition(int index) override {
        (void)index;
        return Vector2{0.0f, 0.0f};
    }
    int GetTouchPointCount() override { return 0; }
    int GetTouchPointId(int index) override {
        (void)index;
        return -1;
    }
    bool IsGamepadAvailable(int gamepad) override {
        (void)gamepad;
        return false;
    }
    float GetGamepadAxisMovement(int gamepad, int axis) override {
        (void)gamepad;
        (void)axis;
        return 0.0f;
    }
    bool IsGamepadButtonDown(int gamepad, int button) override {
        (void)gamepad;
        (void)button;
        return false;
    }
    void SetGamepadVibration(int gamepad, float leftMotor, float rightMotor,
                             float duration) override {
        (void)gamepad;
        (void)leftMotor;
        (void)rightMotor;
        (void)duration;
    }

    // ── overlay drawing ───────────────────────────────────────────────
    void DrawLineEx(Vector2 startPos, Vector2 endPos, float thick,
                    Color color) override {
        (void)startPos;
        (void)endPos;
        (void)thick;
        (void)color;
    }
    void DrawRing(Vector2 center, float innerRadius, float outerRadius,
                  float startAngle, float endAngle, int segments,
                  Color color) override {
        (void)center;
        (void)innerRadius;
        (void)outerRadius;
        (void)startAngle;
        (void)endAngle;
        (void)segments;
        (void)color;
    }
    void DrawCircleV(Vector2 center, float radius, Color color) override {
        (void)center;
        (void)radius;
        (void)color;
    }
    void DrawRectangleV(Vector2 position, Vector2 size, Color color) override {
        (void)position;
        (void)size;
        (void)color;
    }
    void DrawRectangleRounded(Rectangle rec, float roundness, int segments,
                              Color color) override {
        (void)rec;
        (void)roundness;
        (void)segments;
        (void)color;
    }
    void DrawRectangleRoundedLines(Rectangle rec, float roundness, int segments,
                                   Color color) override {
        (void)rec;
        (void)roundness;
        (void)segments;
        (void)color;
    }
    void DrawRectangleLinesEx(Rectangle rec, float lineThick,
                              Color color) override {
        (void)rec;
        (void)lineThick;
        (void)color;
    }

    // Scripting / observation.
    bool window_open = false;
    bool window_ready = true;
    unsigned int texture_id = 1;
    bool shader_valid = true;
    unsigned int shader_id = 0;
    int render_texture_id = 7;
    int load_render_texture_calls = 0;
    int last_rt_w = 0, last_rt_h = 0;
    bool unknown_shader_locations = false;
    const char* shader_source = nullptr;
    int next_uniform_location = 1;
    int uniform_writes = 0;
    std::map<std::string, int> uniform_names;
    std::vector<ShaderUniformWrite> uniform_write_log;
    bool audio_ready = false;
    bool audio_stream_ok = true;
    int monitor_count = 1;
    int refresh_hz = 60;
    bool key_down_held = false;
    bool key_pressed_held = false;
    int press_queue_once = 0;
    bool should_close = false;
    int screen_w = 720, screen_h = 480;
    unsigned int config_flags = 0;
    int target_fps = 0;
    int init_window_calls = 0, set_title_calls = 0, update_texture_calls = 0;
    int set_window_size_calls = 0;
    int set_window_state_calls = 0, clear_window_state_calls = 0;
    unsigned last_set_state = 0, last_clear_state = 0;
    int set_texture_filter_calls = 0, last_texture_filter = 0;
    int last_window_w = 0, last_window_h = 0;
    int last_w = 0, last_h = 0;
};

}  // namespace gbarecomp::testing

#endif  // defined(GBARECOMP_USE_RAYLIB)
