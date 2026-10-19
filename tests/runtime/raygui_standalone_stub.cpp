// raygui_standalone_stub.cpp — raygui's implementation with recording
// primitives, so the raygui menu engine (menu_engine_raygui.cpp) can be
// driven HEADLESS and its draws ASSERTED.
//
// raygui's blessed headless path (RAYGUI_STANDALONE, raygui.h §config):
// the header self-provides the value types and the color/text helpers, and
// the user provides the input/drawing primitives. This TU provides them:
//
//   - DrawRectangle + DrawTextCodepoint — the primitives THIS snapshot's
//     GuiDrawRectangle/GuiDrawText internals bottom out in (they are
//     statics inside raygui.h itself in 5.1-dev) — record every call.
//     These are the assertions' observation point; text runs assemble
//     from the per-codepoint log (raygui_standalone_recorder.h).
//   - The raylib-shaped internals (font/file/input) — compiled into the
//     implementation but never on the menu engine's draw path — no-op or
//     return inert values. A dummy fixed-advance font backs GetFontDefault
//     so text measuring never touches a null glyph table.
//   - Five raylib functions this snapshot calls under STANDALONE without
//     declaring or defining (upstream gap: GetCodepoint, GetGlyphIndex,
//     GetScreenWidth, GetScreenHeight, UnloadTexture) — declared up top so
//     the implementation compiles, defined inertly/correctly below.
//
// The test target compiles menu_engine_raygui.cpp ITSELF with
// RAYGUI_STANDALONE + RAYGUI_NO_ICONS + GBARECOMP_USE_RAYLIB (its compile
// guard) — declarations-only there — so this TU is the single
// implementation, mirroring raygui_impl.cpp's role in the real build.
// GBARECOMP_USE_RAYLIB does NOT pull raylib in here: under STANDALONE
// raygui.h never includes it.
//
// The three defines MUST precede the recorder header (which includes
// raygui.h): this TU is the implementation, the other TUs in the target
// see declarations-only. RAYGUI_STANDALONE/RAYGUI_NO_ICONS also arrive
// via the target's compile definitions for the engine TU — identical
// redefinition is silent.
#define RAYGUI_STANDALONE
#define RAYGUI_NO_ICONS
#define RAYGUI_IMPLEMENTATION

// Upstream STANDALONE gap: called by the implementation, never declared.
// The value types only exist after raygui.h — forward-declare the tagged
// structs (the header's typedefs complete them identically).
struct Font;
struct Texture;
static char* GetClipboardText(void);
static int GetCodepoint(const char* text, int* codepointSize);
static int GetCodepointPrevious(const char* text, int* codepointSize);
static int GetGlyphIndex(Font font, int codepoint);
static int GetScreenWidth(void);
static int GetScreenHeight(void);
static void UnloadTexture(Texture texture);

#include "raygui_standalone_recorder.h"

#include <cstdlib>

namespace gbarecomp {
namespace testing {

std::vector<DrawnRect>& drawn_rects() {
    static std::vector<DrawnRect> v;
    return v;
}

int& shapes_texture_binds() {
    static int n = 0;
    return n;
}

static Font make_stale_font_copy() {
    // A valid but separate glyph table for the dead-context binding. The
    // real bug leaves this pointing at freed launcher memory; a live copy
    // keeps the test's measuring path safe while still differing in
    // pointer identity from GetFontDefault().
    const Font live = GetFontDefault();
    Font stale = live;
    stale.recs = static_cast<Rectangle*>(
        std::malloc(sizeof(Rectangle) * live.glyphCount));
    stale.glyphs = static_cast<GlyphInfo*>(
        std::malloc(sizeof(GlyphInfo) * live.glyphCount));
    for (int i = 0; i < live.glyphCount; ++i) {
        stale.recs[i] = live.recs[i];
        stale.glyphs[i] = live.glyphs[i];
    }
    return stale;
}

void corrupt_font_texture(unsigned int stale_id) {
    // guiFont is raygui's file-static, visible in this TU (the
    // implementation TU). Use a separate valid table so the healing path's
    // cleanup frees only the poisoned copy, never the live default.
    guiFont = make_stale_font_copy();
    guiFont.texture.id = stale_id;
}

void corrupt_font_same_texture() {
    // The driver reused the dead context's texture id, so the id-only
    // rebind guard sees "same font". Point the glyph tables at an
    // identical *different* allocation: the id matches, the recs/glyphs
    // pointers do not, and drawing must heal the latter.
    guiFont = make_stale_font_copy();
}

bool font_binding_is_default() {
    const Font live = GetFontDefault();
    return guiFont.texture.id == live.texture.id &&
           guiFont.recs == live.recs && guiFont.glyphs == live.glyphs;
}

unsigned int font_texture_id() { return guiFont.texture.id; }

// The per-codepoint log, in draw order.
struct DrawnCodepoint {
    int codepoint = 0;
    Vector2 position{};
    float font_size = 0.0f;
    Color color{};
};

std::vector<DrawnCodepoint>& drawn_codepoints() {
    static std::vector<DrawnCodepoint> v;
    return v;
}

std::vector<TextRun>& text_runs() {
    static std::vector<TextRun> v;
    return v;
}

void reset_recording() {
    drawn_rects().clear();
    drawn_codepoints().clear();
    text_runs().clear();
    shapes_texture_binds() = 0;
}

void assemble_text_runs() {
    text_runs().clear();
    if (drawn_codepoints().empty()) return;

    // Group the codepoint log into runs: a run continues while the next
    // codepoint sits on the same line (same y). raygui never DRAWS space
    // glyphs (it just advances), so word boundaries arrive as missing
    // glyphs — reconstruct them the way the launcher harness does: a gap
    // of roughly one extra glyph advance folds a space into the run;
    // anything wider splits it (the dummy font is fixed-advance, so the
    // bands are multiples of the drawn size).
    const DrawnCodepoint* prev = nullptr;
    TextRun run;
    auto flush = [&]() {
        if (!run.text.empty()) text_runs().push_back(run);
        run.text.clear();
    };
    for (const auto& cp : drawn_codepoints()) {
        if (prev != nullptr && cp.position.y == prev->position.y) {
            const float gap = cp.position.x - prev->position.x;
            if (gap > 0.0f && gap <= cp.font_size * 0.9f) {
                // direct continuation: keep the run's origin
            } else if (gap > cp.font_size * 0.9f &&
                       gap <= cp.font_size * 2.5f) {
                run.text.push_back(' ');
            } else {
                flush();
                run = TextRun{"", cp.position, cp.font_size, cp.color};
            }
        } else {
            flush();
            run = TextRun{"", cp.position, cp.font_size, cp.color};
        }
        if (cp.codepoint >= 32 && cp.codepoint < 127)
            run.text.push_back(static_cast<char>(cp.codepoint));
        prev = &cp;
    }
    flush();
}

}  // namespace testing
}  // namespace gbarecomp

// ── The recording primitives (this snapshot's bottom layer) ─────────────

static void DrawRectangle(int x, int y, int width, int height, Color color) {
    gbarecomp::testing::drawn_rects().push_back({x, y, width, height, color});
}

static void DrawRectangleGradientEx(Rectangle rec, Color col1, Color col2,
                                    Color col3, Color col4) {
    (void)rec;
    (void)col1;
    (void)col2;
    (void)col3;
    (void)col4;
}

static void DrawTextCodepoint(Font font, int codepoint, Vector2 position,
                              float fontSize, Color tint) {
    (void)font;
    gbarecomp::testing::drawn_codepoints().push_back(
        {codepoint, position, fontSize, tint});
}

// ── Input (never on the engine's draw path — the contract is locked) ────

static Vector2 GetMousePosition(void) { return Vector2{0.0f, 0.0f}; }
static float GetMouseWheelMove(void) { return 0.0f; }
static bool IsMouseButtonDown(int button) {
    (void)button;
    return false;
}
static bool IsMouseButtonPressed(int button) {
    (void)button;
    return false;
}
static bool IsMouseButtonReleased(int button) {
    (void)button;
    return false;
}
static bool IsKeyDown(int key) {
    (void)key;
    return false;
}
static bool IsKeyPressed(int key) {
    (void)key;
    return false;
}
static int GetCharPressed(void) { return 0; }

// ── The upstream-gap helpers (declared up top) ──────────────────────────

static char* GetClipboardText(void) { return nullptr; }

static int GetCodepointPrevious(const char* text, int* codepointSize) {
    (void)text;
    if (codepointSize) *codepointSize = 0;
    return 0;
}

static int GetCodepoint(const char* text, int* codepointSize) {
    // raygui's own GetCodepointNext is defined by the STANDALONE block.
    return GetCodepointNext(text, codepointSize);
}

static int GetGlyphIndex(Font font, int codepoint) {
    // Matches the dummy font below (95 glyphs at 32+i); anything outside
    // maps to the first glyph so measuring never indexes out of range.
    (void)font;
    if (codepoint >= 32 && codepoint < 127) return codepoint - 32;
    return 0;
}

static int GetScreenWidth(void) { return 720; }
static int GetScreenHeight(void) { return 480; }
static void UnloadTexture(Texture2D texture) { (void)texture; }

// Install the dummy font at static-init time: guiFont (raygui.h's file-scope
// static, visible in this TU) stays zero in the STANDALONE path unless
// GuiLoadStyleDefault runs — and the engine's draw measures text long before
// anything would. Zero glyphs + GetGlyphIndex handing back a real index =
// a null deref on the first measured character.
namespace {
struct GuiFontInit {
    GuiFontInit() { guiFont = GetFontDefault(); }
};
const GuiFontInit gui_font_init;
}  // namespace

// ── Font/file internals ──────────────────────────────────────────────────
// A dummy fixed-advance font: text measuring walks glyphs, and a zero
// font would hand back null glyph pointers. 96 glyphs — the 95 ASCII
// printables plus raygui's white-square glyph at index 95, which
// GuiLoadStyleDefault reads (recs[95]) when it rebinds the font. The
// arrays are HEAP-allocated because GuiLoadStyleDefault RAYGUI_FREEs
// recs/glyphs when it swaps a font in — free() on a static array would
// corrupt the heap the first time a test exercises the swap path.

static GlyphInfo* dummy_glyphs = nullptr;
static Rectangle* dummy_recs = nullptr;

static Font GetFontDefault(void) {
    static Font font = {0};
    if (font.glyphs == nullptr) {
        font.baseSize = 10;
        font.glyphCount = 96;
        font.glyphPadding = 0;
        font.recs = static_cast<Rectangle*>(
            std::calloc(96, sizeof(Rectangle)));
        font.glyphs = static_cast<GlyphInfo*>(
            std::calloc(96, sizeof(GlyphInfo)));
        dummy_recs = font.recs;
        dummy_glyphs = font.glyphs;
        for (int i = 0; i < 96; ++i) {
            font.glyphs[i].value = 32 + i;
            font.glyphs[i].advanceX = (i < 95) ? 6 : 0;
            font.recs[i] = Rectangle{0, 0, (i < 95) ? 6.0f : 1.0f,
                                     (i < 95) ? 10.0f : 1.0f};
        }
    }
    return font;
}

static Font LoadFontEx(const char* fileName, int fontSize, int* codepoints,
                       int codepointCount) {
    (void)fileName;
    (void)fontSize;
    (void)codepoints;
    (void)codepointCount;
    return GetFontDefault();
}

static Texture2D LoadTextureFromImage(Image image) {
    (void)image;
    return Texture2D{0};
}
static void SetShapesTexture(Texture2D tex, Rectangle rec) {
    (void)tex;
    (void)rec;
    ++gbarecomp::testing::shapes_texture_binds();
}
static char* LoadFileText(const char* fileName) {
    (void)fileName;
    return nullptr;
}
static void UnloadFileText(char* text) { (void)text; }
static const char* GetDirectoryPath(const char* filePath) {
    (void)filePath;
    return ".";
}
static int* LoadCodepoints(const char* text, int* count) {
    (void)text;
    if (count) *count = 0;
    return nullptr;
}
static void UnloadCodepoints(int* codepoints) { (void)codepoints; }
static unsigned char* DecompressData(const unsigned char* compData,
                                     int compDataSize, int* dataSize) {
    (void)compData;
    (void)compDataSize;
    if (dataSize) *dataSize = 0;
    return nullptr;
}
