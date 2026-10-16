// launcher_raylib_stub.cpp — the raylib surface the launcher's draw loop
// runs against, headless: every draw call recorded, every input scripted.
//
// The launcher TU (launcher_raygui.cpp) and raygui.h (non-standalone — the
// implementation lives HERE, mirroring raygui_impl.cpp's role in the real
// build) compile against tests/runtime/stub_raylib/raylib.h and link this
// TU. raygui bottoms out in the same recorded primitives (DrawRectangle,
// DrawTextCodepoint), so toolkit widgets are as observable as the
// launcher's own draws.
//
// The dummy fixed-advance font backs GetFontDefault: raygui installs it
// into guiFont on its lazy style init, and measuring a zero font is a null
// deref (the menu harness hit this first — see its stub).
#include "launcher_raylib_stub.h"

// raygui's single implementation TU for this target (mirrors
// raygui_impl.cpp in the real build). The define MUST precede the include:
// raygui.h's include guard makes a later include a no-op. Non-standalone,
// so raygui.h pulls the fake raylib.h and bottoms out in the recorded
// primitives below.
#define RAYGUI_IMPLEMENTATION
#include "raygui/raygui.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

// Install the dummy font at static-init time: guiFont (raygui.h's
// file-scope static, visible in this TU) stays zero until raygui's lazy
// style init runs — and the first widget's text draw measures glyphs
// before that, which is a null deref (the menu harness hit this first).
namespace {
struct GuiFontInit {
    GuiFontInit() { guiFont = GetFontDefault(); }
};
const GuiFontInit gui_font_init;
}  // namespace

namespace gbarecomp {
namespace launcher_stub {

std::vector<DrawnText>& texts() {
    static std::vector<DrawnText> v;
    return v;
}

std::vector<DrawnText>& codepoints() {
    static std::vector<DrawnText> v;
    return v;
}

std::vector<DrawnRect>& rects() {
    static std::vector<DrawnRect> v;
    return v;
}

std::vector<DrawnRing>& rings() {
    static std::vector<DrawnRing> v;
    return v;
}

std::vector<DrawnClear>& clears() {
    static std::vector<DrawnClear> v;
    return v;
}

int& window_opens() {
    static int n = 0;
    return n;
}

int& window_closes() {
    static int n = 0;
    return n;
}

static int& frame_idx() {
    static int f = 0;
    return f;
}

int current_frame() { return frame_idx(); }

// The frame-advance hook lives in EndDrawing (same TU); exposed as a plain
// int& mutator for it.
int& current_frame_int() { return frame_idx(); }

Script& script() {
    static Script s;
    return s;
}

void reset_all() {
    texts().clear();
    codepoints().clear();
    rects().clear();
    rings().clear();
    clears().clear();
    window_opens() = 0;
    window_closes() = 0;
    script() = Script{};
    frame_idx() = 0;
}

static std::uint32_t rgb_of(const Color& c) {
    return (static_cast<std::uint32_t>(c.r) << 16) |
           (static_cast<std::uint32_t>(c.g) << 8) |
           static_cast<std::uint32_t>(c.b);
}

std::vector<std::string> text_runs(int frame) {
    std::vector<std::string> runs;
    for (const auto& t : texts())
        if (t.frame == frame && !t.text.empty())
            runs.push_back(t.text);
    // Fold the codepoint log into runs: a run continues while the next
    // glyph sits on the same line and advances x by roughly a glyph.
    const DrawnText* prev = nullptr;
    std::string run;
    auto flush = [&]() {
        if (!run.empty()) runs.push_back(run);
        run.clear();
    };
    for (const auto& cp : codepoints()) {
        if (cp.frame != frame) continue;
        if (prev != nullptr && cp.y == prev->y) {
            const float gap = cp.x - prev->x;
            // raygui never DRAWS space glyphs (it just advances), so word
            // boundaries arrive as missing glyphs: a gap of roughly one
            // extra glyph advance reconstructs a space; anything wider
            // splits the run.
            if (gap > 0.0f && gap <= cp.size * 0.9f) {
                // direct continuation
            } else if (gap > cp.size * 0.9f && gap <= cp.size * 2.5f) {
                run.push_back(' ');
            } else {
                flush();
            }
        } else {
            flush();
        }
        if (cp.text[0] >= 32 && cp.text[0] < 127)
            run.push_back(cp.text[0]);
        prev = &cp;
    }
    flush();
    return runs;
}

}  // namespace launcher_stub
}  // namespace gbarecomp

// ── Window/system ────────────────────────────────────────────────────────

void InitWindow(int width, int height, const char* title) {
    (void)width;
    (void)height;
    (void)title;
    ++gbarecomp::launcher_stub::window_opens();
}

void CloseWindow(void) { ++gbarecomp::launcher_stub::window_closes(); }

void SetTargetFPS(int fps) { (void)fps; }

bool WindowShouldClose(void) {
    const auto& sc = gbarecomp::launcher_stub::script().should_close;
    const int f = gbarecomp::launcher_stub::current_frame();
    return f >= static_cast<int>(sc.size()) || sc[f];
}

void SetWindowFocused(void) {}

const char* GetApplicationDirectory(void) { return "./"; }

int GetScreenWidth(void) { return 720; }
int GetScreenHeight(void) { return 480; }

// ── Drawing (the recorders) ──────────────────────────────────────────────

// EndDrawing advances the frame index: every draw between Begin/End is
// stamped with the frame it happened on.
void BeginDrawing(void) {}

void EndDrawing(void) { ++gbarecomp::launcher_stub::current_frame_int(); }

void ClearBackground(Color color) {
    gbarecomp::launcher_stub::clears().push_back(
        {gbarecomp::launcher_stub::current_frame(),
         gbarecomp::launcher_stub::rgb_of(color)});
}

void DrawLine(int startPosX, int startPosY, int endPosX, int endPosY,
              Color color) {
    (void)startPosX;
    (void)startPosY;
    (void)endPosX;
    (void)endPosY;
    (void)color;
}

void DrawText(const char* text, int posX, int posY, int fontSize,
              Color color) {
    if (!text) return;
    gbarecomp::launcher_stub::texts().push_back(
        {gbarecomp::launcher_stub::current_frame(), text,
         static_cast<float>(posX), static_cast<float>(posY),
         static_cast<float>(fontSize), gbarecomp::launcher_stub::rgb_of(color)});
}

int MeasureText(const char* text, int fontSize) {
    // Deterministic fake: half the size per byte.
    if (!text) return 0;
    return static_cast<int>(static_cast<float>(fontSize) * 0.5f *
                            static_cast<float>(strlen(text)));
}

void DrawTextCodepoint(Font font, int codepoint, Vector2 position,
                       float fontSize, Color tint) {
    (void)font;
    (void)tint;
    char ch = (codepoint >= 0 && codepoint < 256)
                  ? static_cast<char>(codepoint)
                  : '?';
    gbarecomp::launcher_stub::codepoints().push_back(
        {gbarecomp::launcher_stub::current_frame(), std::string(1, ch),
         position.x, position.y, fontSize,
         gbarecomp::launcher_stub::rgb_of(tint)});
}

void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest,
                    Vector2 origin, float rotation, Color tint) {
    (void)texture;
    (void)source;
    (void)dest;
    (void)origin;
    (void)rotation;
    (void)tint;
}

void DrawRectangle(int posX, int posY, int width, int height, Color color) {
    gbarecomp::launcher_stub::rects().push_back(
        {gbarecomp::launcher_stub::current_frame(),
         static_cast<float>(posX), static_cast<float>(posY),
         static_cast<float>(width), static_cast<float>(height),
         gbarecomp::launcher_stub::rgb_of(color)});
}

void DrawRectangleRec(Rectangle rec, Color color) {
    DrawRectangle(static_cast<int>(rec.x), static_cast<int>(rec.y),
                  static_cast<int>(rec.width), static_cast<int>(rec.height),
                  color);
}

void DrawRectangleRounded(Rectangle rec, float roundness, int segments,
                          Color color) {
    (void)roundness;
    (void)segments;
    gbarecomp::launcher_stub::rects().push_back(
        {gbarecomp::launcher_stub::current_frame(), rec.x, rec.y, rec.width,
         rec.height, gbarecomp::launcher_stub::rgb_of(color)});
}

void DrawRectangleRoundedLinesEx(Rectangle rec, float roundness, int segments,
                                 float lineThick, Color color) {
    (void)roundness;
    (void)segments;
    gbarecomp::launcher_stub::rings().push_back(
        {gbarecomp::launcher_stub::current_frame(), rec.x, rec.y, rec.width,
         rec.height, lineThick, gbarecomp::launcher_stub::rgb_of(color)});
}

void DrawRectangleLinesEx(Rectangle rec, float lineThick, Color color) {
    gbarecomp::launcher_stub::rings().push_back(
        {gbarecomp::launcher_stub::current_frame(), rec.x, rec.y, rec.width,
         rec.height, lineThick, gbarecomp::launcher_stub::rgb_of(color)});
}

void DrawRectangleGradientEx(Rectangle rec, Color topLeft, Color bottomLeft,
                             Color topRight, Color bottomRight) {
    (void)rec;
    (void)topLeft;
    (void)bottomLeft;
    (void)topRight;
    (void)bottomRight;
}

// ── Files/drops ──────────────────────────────────────────────────────────

bool IsFileDropped(void) { return false; }
FilePathList LoadDroppedFiles(void) { return FilePathList{0, 0, nullptr}; }
void UnloadDroppedFiles(FilePathList files) { (void)files; }

Image LoadImage(const char* fileName) {
    (void)fileName;
    return Image{nullptr, 0, 0, 0, 0};  // no box art in the harness
}

void UnloadImage(Image image) { (void)image; }

Texture2D LoadTextureFromImage(Image image) {
    (void)image;
    return Texture2D{0, 0, 0, 0, 0};
}

void UnloadTexture(Texture2D texture) { (void)texture; }

// ── Input (scripted) ─────────────────────────────────────────────────────

static bool vector_has(const std::vector<int>& v, int value) {
    for (int x : v)
        if (x == value) return true;
    return false;
}

bool IsKeyPressed(int key) {
    const auto& kp = gbarecomp::launcher_stub::script().keys_pressed;
    const int f = gbarecomp::launcher_stub::current_frame();
    return f < static_cast<int>(kp.size()) && vector_has(kp[f], key);
}

bool IsKeyDown(int key) { return IsKeyPressed(key); }
int GetCharPressed(void) { return 0; }

Vector2 GetMousePosition(void) {
    const auto& mp = gbarecomp::launcher_stub::script().mouse_position;
    const int f = gbarecomp::launcher_stub::current_frame();
    if (f < static_cast<int>(mp.size())) return Vector2{mp[f].first, mp[f].second};
    return Vector2{0.0f, 0.0f};
}

float GetMouseWheelMove(void) { return 0.0f; }

bool IsMouseButtonDown(int button) { (void)button; return false; }

bool IsMouseButtonPressed(int button) {
    const auto& mb = gbarecomp::launcher_stub::script().mouse_buttons_pressed;
    const int f = gbarecomp::launcher_stub::current_frame();
    return f < static_cast<int>(mb.size()) && vector_has(mb[f], button);
}

bool IsMouseButtonReleased(int button) { (void)button; return false; }

bool IsGamepadAvailable(int gamepad) { (void)gamepad; return false; }

bool IsGamepadButtonPressed(int gamepad, int button) {
    (void)gamepad;
    (void)button;
    return false;
}

bool IsGamepadButtonReleased(int gamepad, int button) {
    (void)gamepad;
    (void)button;
    return false;
}

bool IsGamepadButtonDown(int gamepad, int button) {
    (void)gamepad;
    (void)button;
    return false;
}

int GetGamepadAxisCount(int gamepad) {
    (void)gamepad;
    return 0;
}

float GetGamepadAxisMovement(int gamepad, int axis) {
    (void)gamepad;
    (void)axis;
    return 0.0f;
}

int GetCodepointNext(const char* text, int* codepointSize) {
    if (codepointSize) *codepointSize = 1;
    return text ? static_cast<unsigned char>(text[0]) : 0;
}

const char* CodepointToUTF8(int codepoint, int* byteSize) {
    (void)codepoint;
    if (byteSize) *byteSize = 0;
    return "";
}

bool TextIsEqual(const char* text1, const char* text2) {
    return text1 && text2 && strcmp(text1, text2) == 0;
}

unsigned int TextLength(const char* text) {
    return text ? static_cast<unsigned int>(strlen(text)) : 0;
}

const char* TextToLower(const char* text) { return text; }
const char* TextToUpper(const char* text) { return text; }

const char* TextSubtext(const char* text, int position, int length) {
    (void)position;
    (void)length;
    return text ? text : "";
}

Color ColorAlpha(Color color, float alpha) {
    Color out = color;
    out.a = static_cast<unsigned char>(alpha * 255.0f);
    return out;
}

// ── Text/font internals ──────────────────────────────────────────────────

static GlyphInfo stub_glyphs[95];
static Rectangle stub_recs[95];

Font GetFontDefault(void) {
    static Font font = {0, 0, 0, Texture2D{0, 0, 0, 0, 0}, nullptr, nullptr};
    if (font.glyphs == nullptr) {
        font.baseSize = 10;
        font.glyphCount = 95;
        font.glyphPadding = 0;
        font.recs = stub_recs;
        font.glyphs = stub_glyphs;
        for (int i = 0; i < 95; ++i) {
            stub_glyphs[i].value = 32 + i;
            stub_glyphs[i].advanceX = 6;
            stub_recs[i] = Rectangle{0, 0, 6, 10};
        }
    }
    return font;
}

Font LoadFontEx(const char* fileName, int fontSize, int* codepoints,
                int codepointCount) {
    (void)fileName;
    (void)fontSize;
    (void)codepoints;
    (void)codepointCount;
    return GetFontDefault();
}

void SetShapesTexture(Texture2D texture, Rectangle source) {
    (void)texture;
    (void)source;
}

int GetCodepoint(const char* text, int* codepointSize) {
    // ASCII fast path is enough for the harness's own labels.
    if (codepointSize) *codepointSize = 1;
    return text ? static_cast<unsigned char>(text[0]) : 0;
}

int GetCodepointPrevious(const char* text, int* codepointSize) {
    (void)text;
    if (codepointSize) *codepointSize = 0;
    return 0;
}

int GetGlyphIndex(Font font, int codepoint) {
    (void)font;
    if (codepoint >= 32 && codepoint < 127) return codepoint - 32;
    return 0;
}

const char* TextFormat(const char* text, ...) {
    static char buffers[8][256];
    static int rotation = 0;
    char* buf = buffers[rotation];
    rotation = (rotation + 1) % 8;
    va_list args;
    va_start(args, text);
    vsnprintf(buf, sizeof(buffers[0]), text, args);
    va_end(args);
    return buf;
}

int TextToInteger(const char* text) { return text ? atoi(text) : 0; }

float TextToFloat(const char* text) { return text ? strtof(text, nullptr) : 0.0f; }

Color GetColor(int hexValue) {
    return Color{static_cast<unsigned char>((hexValue >> 24) & 0xFF),
                 static_cast<unsigned char>((hexValue >> 16) & 0xFF),
                 static_cast<unsigned char>((hexValue >> 8) & 0xFF),
                 static_cast<unsigned char>(hexValue & 0xFF)};
}

Color Fade(Color color, float alpha) {
    Color out = color;
    out.a = static_cast<unsigned char>(static_cast<float>(out.a) * alpha);
    return out;
}

int ColorToInt(Color color) {
    return (static_cast<int>(color.r) << 24) | (static_cast<int>(color.g) << 16) |
           (static_cast<int>(color.b) << 8) | static_cast<int>(color.a);
}

bool CheckCollisionPointRec(Vector2 point, Rectangle rec) {
    return point.x >= rec.x && point.x < rec.x + rec.width &&
           point.y >= rec.y && point.y < rec.y + rec.height;
}

char* GetClipboardText(void) { return nullptr; }

char* LoadFileText(const char* fileName) {
    (void)fileName;
    return nullptr;
}

void UnloadFileText(char* text) { (void)text; }

unsigned char* LoadFileData(const char* fileName, int* dataSize) {
    (void)fileName;
    if (dataSize) *dataSize = 0;
    return nullptr;
}

void UnloadFileData(unsigned char* data) { (void)data; }

int SaveFileData(const char* fileName, void* data, int dataSize) {
    (void)fileName;
    (void)data;
    (void)dataSize;
    return 0;
}

const char* GetDirectoryPath(const char* filePath) {
    (void)filePath;
    return ".";
}

int* LoadCodepoints(const char* text, int* count) {
    (void)text;
    if (count) *count = 0;
    return nullptr;
}

void UnloadCodepoints(int* codepoints) { (void)codepoints; }

unsigned char* DecompressData(const unsigned char* compData, int compDataSize,
                              int* dataSize) {
    (void)compData;
    (void)compDataSize;
    if (dataSize) *dataSize = 0;
    return nullptr;
}
