#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "presentation_layout.h"

// LCD (subpixel-grid) shader policy + GLSL source, shared by nothing yet but
// the raylib backend. Pure and backend-free so it runs with no display.
//
// Reference: Shadertoy "LCD Post Effect" (ldBfDd), via its Godot port. The
// effect is a screen-space RGB subpixel mask repeating every 3 screen pixels,
// with every other row flattened to a dark level:
//
//   vec4 lcdColor = vec4(pb, pb, pb, 1.0);
//   int px = int(mod(FRAGCOORD.x, 3.0));
//   if (px == 1) lcdColor.r = 1.0;       // x = 1 mod 3 -> red
//   else if (px == 2) lcdColor.g = 1.0;  // x = 2 mod 3 -> green
//   else lcdColor.b = 1.0;               // x = 0 mod 3 -> blue
//   if (int(mod(FRAGCOORD.y, 2.0)) == 0)
//       lcdColor.rgb = vec3(sclV, sclV, sclV);
//   COLOR = texColor * lcdColor;
//
// The column order (blue, red, green) is the reference's, kept verbatim.
// Pixel order on a real panel is panel-specific anyway; what matters is the
// 3-pixel period, the 2-row period, and the two levels.
//
// What changed from the reference is the SPACE those periods live in. The
// reference draws at 1:1, where one screen pixel is one game pixel and the
// periods are therefore already game-pixel-sized. FFTA is presented scaled
// (3x by default, 6x/8x/9x by request, and a fractional fit in every window
// that is not a whole multiple), so indexing gl_FragCoord with the reference's
// periods produced a 3-DRAWABLE-pixel grille that drifted across the artwork
// and only coincided with the game grid by accident at exactly 3x. The mask
// below is anchored to the game grid instead (PresentGrid), which at 1:1
// reduces to the reference verbatim.
//
// The GLSL below is that logic translated to raylib's GLSL330 convention
// (texture0 / fragTexCoord / colDiffuse / finalColor, gl_FragCoord in screen
// pixels). Desktop-only: OpenGL ES needs a different #version and texture()
// spelling, so the backend skips compiling this on Android (logged, stays
// off) until that variant exists.

namespace gbarecomp {

// Shadertoy defaults, lifted twice (see below): the reference ran
// pb = 0.4 / sclV = 0.25, which passes only ~0.43x of the light and reads as
// dim. A pre-mask gain was tried first and rejected — it clips every texel
// channel above 1/gain into flat white (blown highlights). Lifting the mask
// floors instead brightens mids and shadows while the full-strength subpixel
// stays 1.0x the source, so highlights are identical to unshaded and nothing
// can clip. The rows were then lifted again (0.5 -> 0.7): screenshot
// measurement showed the 0.5 rows striping mid-grey by 30 LSB every other
// line, dominating flat backgrounds. At 0.7 the residual is ~4 LSB — texture
// without banding.
struct LcdShaderParams {
    float base_level = 0.6f;
    float dark_row_level = 0.7f;
};

struct LcdMask {
    float r = 1.0f, g = 1.0f, b = 1.0f;
};

// True modulo for possibly-negative screen coords (C++ % keeps the sign).
inline int lcd_floor_mod(int v, int m) {
    int r = v % m;
    return r < 0 ? r + m : r;
}

// Float true modulo -- the same rule GLSL's mod() implements, so the CPU
// mirrors and the shaders agree on negative coordinates.
inline float lcd_floor_mod_f(float v, float m) {
    const float r = std::fmod(v, m);
    return r < 0.0f ? r + m : r;
}

// ---- the presented game grid ----------------------------------------------
// Everything the GPU passes need to know about scale: where the logical
// framebuffer starts on the drawable, and how many drawable pixels one game
// pixel covers. compute_presentation_layout() puts the image on screen at the
// largest whole multiple of its reduced aspect, so the scale is whatever that
// yields -- 3x/6x/8x/9x for the usual window requests, but 6.75 on 1080p
// fullscreen and essentially anything else in a dragged window.
//
// Resolved once per frame and uploaded as (uGrid, uGameSize): uGrid is
// (origin_x, origin_y, scale_x, scale_y), uGameSize is (game_w, game_h). A
// PresentGrid with no extent (no logical size or a zero scale) means "no
// rectangle to honour", and the mask then covers the whole drawable.
struct PresentGrid {
    int origin_x = 0, origin_y = 0;        // image top-left, drawable pixels
    int game_w = 0, game_h = 0;            // logical framebuffer, game pixels
    float scale_x = 0.0f, scale_y = 0.0f;  // drawable pixels per game pixel
};

inline PresentGrid resolve_present_grid(const PresentationLayout& layout,
                                        int base_w, int base_h) {
    PresentGrid g;
    g.origin_x = layout.x;
    g.origin_y = layout.y;
    g.game_w = base_w > 0 ? base_w : 0;
    g.game_h = base_h > 0 ? base_h : 0;
    if (g.game_w > 0 && layout.width > 0)
        g.scale_x = static_cast<float>(layout.width) /
                    static_cast<float>(g.game_w);
    if (g.game_h > 0 && layout.height > 0)
        g.scale_y = static_cast<float>(layout.height) /
                    static_cast<float>(g.game_h);
    return g;
}

// A grid whose extent is unknown masks everything; the bars exemption needs a
// rectangle to compare against.
inline bool grid_has_extent(const PresentGrid& g) {
    return g.game_w > 0 && g.game_h > 0 && g.scale_x > 0.0f && g.scale_y > 0.0f;
}

// True when the fragment lies inside the presented image. This is what keeps
// the LCD grid and the dither off the letterbox bars on the fullscreen pass.
inline bool lcd_inside_grid(float screen_x, float screen_y,
                            const PresentGrid& g) {
    if (!grid_has_extent(g)) return true;
    const float px = screen_x - static_cast<float>(g.origin_x);
    const float py = screen_y - static_cast<float>(g.origin_y);
    return px >= 0.0f && py >= 0.0f &&
           px < static_cast<float>(g.game_w) * g.scale_x &&
           py < static_cast<float>(g.game_h) * g.scale_y;
}

// Continuous game-pixel coordinates. A missing scale falls back to the raw
// offset, which keeps a degenerate grid behaving like the old screen space.
inline float lcd_grid_game_x(float screen_x, const PresentGrid& g) {
    const float px = screen_x - static_cast<float>(g.origin_x);
    return g.scale_x > 0.0f ? px / g.scale_x : px;
}
inline float lcd_grid_game_y(float screen_y, const PresentGrid& g) {
    const float py = screen_y - static_cast<float>(g.origin_y);
    return g.scale_y > 0.0f ? py / g.scale_y : py;
}

// Distance from x to the nearest centre of a periodic set (centre, centre +
// period, ...), floor-mod so negatives wrap. This is what makes the mask
// periodic in GAME space with no seam at the cell edge.
inline float lcd_periodic_distance(float x, float centre, float period) {
    const float half = period * 0.5f;
    const float d = lcd_floor_mod_f(x - centre + half, period) - half;
    return d < 0.0f ? -d : d;
}

// A stripe of width 1 (in its own units) under a fragment `width` units wide
// whose centre is `distance` away: the overlap is width/2 + 0.5 - distance,
// capped at the fragment's own width, so the three coverages sum to 1 exactly
// (no net dimming, and no clipping: the full-strength channel stays 1.0).
inline float lcd_stripe_coverage(float distance, float width) {
    if (!(width > 0.0f)) return 0.0f;
    const float f = (width * 0.5f + 0.5f - distance) / width;
    if (!(f > 0.0f)) return 0.0f;
    return f < 1.0f ? f : 1.0f;
}

// A triad stripe is scale_x/3 drawable pixels wide. Only from 3 drawable
// pixels per game pixel is that a whole pixel; below it a triad cannot be
// drawn at all, so the mask collapses to one stripe per game pixel -- which is
// exactly the Shadertoy reference's ratio at 1:1.
inline constexpr float kLcdMinTriadScale = 3.0f;

// The real mask: game-space anchored, anti-aliased against the fragment's own
// footprint. Screen coordinates are continuous drawable pixels (gl_FragCoord's
// convention). Fragments outside the presented image are unshaded.
inline LcdMask lcd_pixel_mask_game(float screen_x, float screen_y,
                                   const PresentGrid& g,
                                   LcdShaderParams p = LcdShaderParams{}) {
    if (!lcd_inside_grid(screen_x, screen_y, g))
        return LcdMask{1.0f, 1.0f, 1.0f};
    const float gx = lcd_grid_game_x(screen_x, g);
    const float gy = lcd_grid_game_y(screen_y, g);

    // ---- columns: one RGB triad per game pixel ----
    LcdMask stripe;
    if (g.scale_x >= kLcdMinTriadScale) {
        // Measure in stripe units (3 stripes per game pixel, stripe width 1),
        // where the fragment's own footprint is one drawable pixel = 3/scale_x.
        const float f = gx * 3.0f;
        const float fw = std::min(1.0f, 3.0f / g.scale_x);
        const float blue =
            lcd_stripe_coverage(lcd_periodic_distance(f, 0.5f, 3.0f), fw);
        const float red =
            lcd_stripe_coverage(lcd_periodic_distance(f, 1.5f, 3.0f), fw);
        const float green =
            lcd_stripe_coverage(lcd_periodic_distance(f, 2.5f, 3.0f), fw);
        // Channel order is the reference's: the stripe at f = 0.5 keeps blue
        // at full strength, 1.5 keeps red, 2.5 keeps green.
        stripe = LcdMask{red + (blue + green) * p.base_level,
                         green + (blue + red) * p.base_level,
                         blue + (red + green) * p.base_level};
    } else {
        switch (lcd_floor_mod(static_cast<int>(std::floor(gx)), 3)) {
            case 1:
                stripe = LcdMask{1.0f, p.base_level, p.base_level};
                break;
            case 2:
                stripe = LcdMask{p.base_level, 1.0f, p.base_level};
                break;
            default:
                stripe = LcdMask{p.base_level, p.base_level, 1.0f};
                break;
        }
    }

    // ---- rows: one flat dark row per game row, every other row ----
    float dark = 0.0f;
    if (g.scale_y >= 1.0f) {
        const float rw = std::min(1.0f, 1.0f / g.scale_y);
        dark = lcd_stripe_coverage(lcd_periodic_distance(gy, 0.5f, 2.0f), rw);
    } else {
        dark = lcd_floor_mod(static_cast<int>(std::floor(gy)), 2) == 0 ? 1.0f
                                                                       : 0.0f;
    }

    const float d = dark * p.dark_row_level;
    const float keep = 1.0f - dark;
    return LcdMask{keep * stripe.r + d, keep * stripe.g + d,
                   keep * stripe.b + d};
}

// Per-PIXEL multiplier at 1:1 (the pinned Shadertoy shape). Coordinates are
// integer indices here while the shaders work in continuous drawable
// coordinates, hence the half-pixel shift; the tests pin that this equals the
// game-space mask on a 1:1 grid at every pixel centre.
inline LcdMask lcd_pixel_mask(int screen_x, int screen_y,
                              LcdShaderParams p = LcdShaderParams{}) {
    const PresentGrid unit{0, 0, 0, 0, 1.0f, 1.0f};
    return lcd_pixel_mask_game(static_cast<float>(screen_x) + 0.5f,
                               static_cast<float>(screen_y) + 0.5f, unit, p);
}

// ---- enablement -----------------------------------------------------------
// Same env convention as GBARECOMP_TOUCH_CONTROLS: set and not "0" means on.
// A set env var (including "0") overrides the config file; unset defers to it.
inline bool lcd_shader_env_on(const char* env) {
    return env && *env && *env != '0';
}
inline bool lcd_shader_env_set(const char* env) { return env && *env; }

// One [video] line. Returns true when the key is ours (so callers can ignore
// the rest); writes the parsed value to *out. Accepts 1/true/on (and the
// 0/false/off mirrors); anything else leaves *out untouched but still claims
// the key, so a typo cannot silently fall through to another section reader.
inline bool apply_lcd_shader_line(const char* key, const char* val,
                                  bool* out) {
    if (!key || !val || !out) return false;
    auto ci_equal = [](const char* a, const char* b) {
        while (*a && *b) {
            char ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
            char cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
            if (ca != cb) return false;
            ++a; ++b;
        }
        return *a == *b;
    };
    auto ci_value = [&](const char* w) { return ci_equal(val, w); };
    if (!ci_equal(key, "lcd_shader")) return false;
    if (ci_value("1") || ci_value("true") || ci_value("on") ||
        ci_value("yes"))
        *out = true;
    else if (ci_value("0") || ci_value("false") || ci_value("off") ||
             ci_value("no"))
        *out = false;
    return true;
}

// Startup default: env wins when set, else the config value (false when no
// config was read).
inline bool resolve_lcd_shader(bool config_value, const char* env) {
    if (lcd_shader_env_set(env)) return lcd_shader_env_on(env);
    return config_value;
}

// ---- ordered dithering (gradient banding) -----------------------------------
// GBA frames are BGR555 — 32 levels per channel — so smooth background
// gradients show contour bands no later stage can un-bake. The fix is a Bayer
// matrix scaled to the SOURCE step, not the display step: one BGR555 level
// spans 255/31 ≈ 8 display LSB, so ±½ display LSB (the textbook amplitude for
// 8-bit-quantized gradients) is invisible here — verified live, 0 vs 2
// indistinguishable. Strength 1.0 therefore means ±4 LSB (half a source
// step), which re-averages into apparent intermediate tones at the cost of
// visible grain on flat fields; that trade is inherent to 15-bit sources.
// Position-based, no texture or history; applied BEFORE the triad mask, in
// game space (see lcd_dither_game), where the bands are visible. It feeds the
// framebuffer's own 8-bit quantization — the final rounding step.
// Pure CPU mirrors for tests (properties pinned, not bit-exactness: GPUs may
// round the fract differently in the last ulp). Screen coords are >= 0.
inline float lcd_bayer2(float ax, float ay) {
    ax = std::floor(ax);
    ay = std::floor(ay);
    const float d = ax * 0.5f + ay * ay * 0.75f;
    return d - std::floor(d);
}

inline float lcd_bayer4(int x, int y) {
    const float bx = static_cast<float>(x & 3);
    const float by = static_cast<float>(y & 3);
    return lcd_bayer2(bx * 0.5f, by * 0.5f) * 0.25f +
           lcd_bayer2(bx, by);
}

// One dither step: Bayer value centred on zero, scaled by the strength and
// the BGR555 source step (kLcdSourceStepLSB), NOT the display LSB — see the
// section note. Applied per channel, clamped to display range.
inline constexpr float kLcdSourceStepLSB = 8.0f;  // 255/31 ≈ 8.2, floored

inline float lcd_dither_step(int x, int y, float strength) {
    return (lcd_bayer4(x, y) - 0.5f) * strength * kLcdSourceStepLSB;
}

inline float lcd_apply_dither(float channel, int x, int y, float strength) {
    const float v = channel + lcd_dither_step(x, y, strength) / 255.0f;
    if (!(v < 1.0f)) return 1.0f;
    return v < 0.0f ? 0.0f : v;
}

// The dither cell is indexed by GAME pixel, not by drawable pixel. A frame
// presented with nearest scaling repeats each game pixel over a whole block,
// so a screen-space 4x4 cell straddles pixel edges and lays uncorrelated noise
// inside a single pixel -- the one thing a square-pixel presentation must not
// do. One cell per game pixel is constant across the block, and the 4x4 cell
// still sums to 0.5, so the mean (and therefore the brightness) is unchanged.
inline float lcd_dither_game(float screen_x, float screen_y,
                             const PresentGrid& g) {
    const int gx = static_cast<int>(std::floor(lcd_grid_game_x(screen_x, g)));
    const int gy = static_cast<int>(std::floor(lcd_grid_game_y(screen_y, g)));
    return lcd_bayer4(gx, gy);  // the &3 inside wraps negatives like mod(g, 4)
}

// One dither step in game space: same strength/LSB scaling as
// lcd_dither_step, indexed by the fragment's game pixel. Fragments outside the
// presented image are left exactly alone (bars must not gain grain).
inline float lcd_dither_step_game(float channel, float screen_x,
                                  float screen_y, const PresentGrid& g,
                                  float strength) {
    if (!lcd_inside_grid(screen_x, screen_y, g)) return channel;
    const float step = (lcd_dither_game(screen_x, screen_y, g) - 0.5f) *
                       strength * kLcdSourceStepLSB;
    const float v = channel + step / 255.0f;
    if (!(v < 1.0f)) return 1.0f;
    return v < 0.0f ? 0.0f : v;
}

inline constexpr float kLcdDefaultDither = 1.0f;
inline constexpr float kLcdMinDither = 0.0f;
inline constexpr float kLcdMaxDither = 2.0f;

inline float lcd_clamp_dither(float d) {
    if (!(d > kLcdMinDither)) return kLcdMinDither;  // NaN-safe floor
    if (d > kLcdMaxDither) return kLcdMaxDither;
    return d;
}

// One [video] line for the dither strength. Same claim-the-key contract.
inline bool apply_lcd_dither_line(const char* key, const char* val,
                                  float* out) {
    if (!key || !val || !out) return false;
    const char* k = key;
    const char* want = "lcd_dither";
    while (*k && *want) {
        char ca = *k >= 'A' && *k <= 'Z' ? *k + 32 : *k;
        if (ca != *want) return false;
        ++k; ++want;
    }
    if (*k != *want) return false;
    float v = 0.0f;
    int n = 0;
    if (std::sscanf(val, "%f%n", &v, &n) != 1 || val[n] != '\0') return true;
    *out = lcd_clamp_dither(v);
    return true;
}
// ---- adaptive vertical deband -------------------------------------------------
// Non-dither banding fix (flash3kyuu/madVR family, stripped to our case):
// bands here are horizontal, so compare each pixel to its vertical neighbors
// and blend toward their average only where they already agree (threshold).
// Sprites/text (strong vertical edges) fail the gate and pass through.
// Pure mirrors: the 3-tap mix and the threshold plumbing.
inline float lcd_deband_maxdiff(float c0r, float c0g, float c0b, float avr,
                                float avg, float avb) {
    float m = c0r - avr;
    m = m < 0.0f ? -m : m;
    float d = c0g - avg;
    d = d < 0.0f ? -d : d;
    if (d > m) m = d;
    d = c0b - avb;
    d = d < 0.0f ? -d : d;
    if (d > m) m = d;
    return m;
}

// Mix factor 1 on flat ground, 0 past the threshold, linear between.
// Threshold t is in display units (0..1); t <= 0 keeps the source untouched.
inline float lcd_deband_factor(float maxdiff, float t) {
    if (!(t > 0.0f)) return 0.0f;
    float f = 1.0f - maxdiff / t;
    if (!(f > 0.0f)) return 0.0f;
    return f < 1.0f ? f : 1.0f;
}

inline constexpr float kLcdDefaultDeband = 0.0f;  // off: softening must opt in
inline constexpr float kLcdMinDeband = 0.0f;
inline constexpr float kLcdMaxDeband = 16.0f;  // threshold in display LSB

inline float lcd_clamp_deband(float d) {
    if (!(d > kLcdMinDeband)) return kLcdMinDeband;  // NaN-safe floor
    if (d > kLcdMaxDeband) return kLcdMaxDeband;
    return d;
}

// One [video] line. Same claim-the-key contract.
inline bool apply_lcd_deband_line(const char* key, const char* val,
                                  float* out) {
    if (!key || !val || !out) return false;
    const char* k = key;
    const char* want = "lcd_deband";
    while (*k && *want) {
        char ca = *k >= 'A' && *k <= 'Z' ? *k + 32 : *k;
        if (ca != *want) return false;
        ++k; ++want;
    }
    if (*k != *want) return false;
    float v = 0.0f;
    int n = 0;
    if (std::sscanf(val, "%f%n", &v, &n) != 1 || val[n] != '\0') return true;
    *out = lcd_clamp_deband(v);
    return true;
}

// The deband pass compares a game texel against its vertical neighbours, so
// its step must be ONE GAME PIXEL expressed in the source texture's UV space.
// Deliberately independent of the on-screen scale, and correct on both
// present() paths: against the game texture (base_w texels) it is 1/base_w,
// and against the sharp pre-upscale target (base_w * factor texels) the same
// 1/base_w spans exactly `factor` texels -- still one game pixel. Deriving it
// from the drawn size instead would silently widen the taps every time the
// window grows, so it is pinned by a test rather than recomputed per frame.
struct TexelStep {
    float x = 0.0f, y = 0.0f;
};

inline TexelStep shader_deband_texel_step(int game_w, int game_h) {
    TexelStep s;
    if (game_w > 0) s.x = 1.0f / static_cast<float>(game_w);
    if (game_h > 0) s.y = 1.0f / static_cast<float>(game_h);
    return s;
}

// ---- pre-mask gain (dim-panel tunable, default off) -------------------------
// The mask levels above are lifted from the reference values for brightness
// (see LcdShaderParams). The sampled texel additionally takes a pre-mask gain
// (`uniform float boost`, default 1.0 = off) for dim panels — kept as a tunable
// because it costs one uniform, but left at unity by default: any gain above
// 1.0 clips texel channels above 1/gain into flat white on the full-strength
// subpixel. Prefer the mask floors for brightness; reach for boost only if the
// panel itself is dark.
inline constexpr float kLcdDefaultBoost = 1.0f;
inline constexpr float kLcdMinBoost = 1.0f;
inline constexpr float kLcdMaxBoost = 3.0f;

inline float lcd_clamp_boost(float boost) {
    if (!(boost > kLcdMinBoost)) return kLcdMinBoost;  // NaN-safe floor
    if (boost > kLcdMaxBoost) return kLcdMaxBoost;
    return boost;
}

inline float lcd_apply_boost(float channel, float boost) {
    const float v = channel * boost;
    if (!(v < 1.0f)) return 1.0f;  // clamps overflow AND NaN
    return v < 0.0f ? 0.0f : v;
}

// One [video] line for the gain. Same claim-the-key contract as
// apply_lcd_shader_line: garbage leaves *out untouched.
inline bool apply_lcd_boost_line(const char* key, const char* val,
                                 float* out) {
    if (!key || !val || !out) return false;
    const char* k = key;
    const char* want = "lcd_boost";
    while (*k && *want) {
        char ca = *k >= 'A' && *k <= 'Z' ? *k + 32 : *k;
        if (ca != *want) return false;
        ++k; ++want;
    }
    if (*k != *want) return false;
    float v = 0.0f;
    int n = 0;
    if (std::sscanf(val, "%f%n", &v, &n) != 1 || val[n] != '\0') return true;
    *out = lcd_clamp_boost(v);
    return true;
}

// GLSL ES 1.00 variants for Android (RL_OPENGL_ES2). Same formulas, only
// syntax changes: explicit precision, varying instead of in, texture2D,
// gl_FragColor. highp (not mediump): the dither hash squares gl_FragCoord
// (1440^2 overflows mediump's 65504 max), and every minSdk-28-era GPU
// supports highp fragment shaders. raylib's default vertex shader already
// provides fragTexCoord/fragColor, so LoadShaderFromMemory(nullptr, frag)
// keeps working.
//
// ES 1.00 is never compiled on the desktop tiers, so these bodies are the
// CPU-mirrored formulas transliterated (and lcd_glsl_tests compiles them with
// glslangValidator when that tool is installed -- see that test for why it
// exists).
inline constexpr const char* kLcdShaderFragmentES100 =
    "#version 100\n"
    "precision highp float;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float boost;\n"
    "uniform vec4 uGrid;\n"
    "uniform vec2 uGameSize;\n"
    "float lcdCoverage(float d, float w) {\n"
    "    if (!(w > 0.0)) return 0.0;\n"
    "    return clamp((w * 0.5 + 0.5 - d) / w, 0.0, 1.0);\n"
    "}\n"
    "float lcdDist(float x, float c, float p) {\n"
    "    return abs(mod(x - c + p * 0.5, p) - p * 0.5);\n"
    "}\n"
    "void main() {\n"
    "    vec4 tex = texture2D(texture0, fragTexCoord) * colDiffuse;\n"
    "    tex.rgb = clamp(tex.rgb * boost, 0.0, 1.0);\n"
    "    vec2 p = gl_FragCoord.xy - uGrid.xy;\n"
    "    vec2 ext = uGameSize * uGrid.zw;\n"
    "    if (!(p.x >= 0.0 && p.y >= 0.0 && p.x < ext.x && p.y < ext.y)) {\n"
    "        gl_FragColor = tex;\n"
    "        return;\n"
    "    }\n"
    "    vec2 g = p / uGrid.zw;\n"
    "    vec3 stripe;\n"
    "    if (uGrid.z >= 3.0) {\n"
    "        float w = min(1.0, 3.0 / uGrid.z);\n"
    "        float f = g.x * 3.0;\n"
    "        stripe = vec3(0.6, 0.6, 1.0) * lcdCoverage(lcdDist(f, 0.5, 3.0), w)\n"
    "               + vec3(1.0, 0.6, 0.6) * lcdCoverage(lcdDist(f, 1.5, 3.0), w)\n"
    "               + vec3(0.6, 1.0, 0.6) * lcdCoverage(lcdDist(f, 2.5, 3.0), w);\n"
    "    } else {\n"
    "        float c = mod(floor(g.x), 3.0);\n"
    "        if (c < 1.0) stripe = vec3(0.6, 0.6, 1.0);\n"
    "        else if (c < 2.0) stripe = vec3(1.0, 0.6, 0.6);\n"
    "        else stripe = vec3(0.6, 1.0, 0.6);\n"
    "    }\n"
    "    float dark;\n"
    "    if (uGrid.w >= 1.0) {\n"
    "        float w = min(1.0, 1.0 / uGrid.w);\n"
    "        dark = lcdCoverage(lcdDist(g.y, 0.5, 2.0), w);\n"
    "    } else {\n"
    "        dark = mod(floor(g.y), 2.0) < 1.0 ? 1.0 : 0.0;\n"
    "    }\n"
    "    vec3 mask = mix(stripe, vec3(0.7), dark);\n"
    "    gl_FragColor = vec4(tex.rgb * mask, tex.a);\n"
    "}\n";

inline constexpr const char* kDitherFragmentES100 =
    "#version 100\n"
    "precision highp float;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float dither;\n"
    "uniform vec4 uGrid;\n"
    "uniform vec2 uGameSize;\n"
    "float bayer2(vec2 a) {\n"
    "    a = floor(a);\n"
    "    return fract(a.x * 0.5 + a.y * a.y * 0.75);\n"
    "}\n"
    "float bayer4(vec2 a) {\n"
    "    return bayer2(0.5 * a) * 0.25 + bayer2(a);\n"
    "}\n"
    "void main() {\n"
    "    vec4 tex = texture2D(texture0, fragTexCoord) * colDiffuse;\n"
    "    vec2 p = gl_FragCoord.xy - uGrid.xy;\n"
    "    vec2 ext = uGameSize * uGrid.zw;\n"
    "    if (p.x >= 0.0 && p.y >= 0.0 && p.x < ext.x && p.y < ext.y) {\n"
    "        vec2 g = p / uGrid.zw;\n"
    "        tex.rgb += (bayer4(mod(g, 4.0)) - 0.5) * dither * 8.0 / 255.0;\n"
    "    }\n"
    "    gl_FragColor = tex;\n"
    "}\n";

inline constexpr const char* kDebandFragmentES100 =
    "#version 100\n"
    "precision highp float;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float deband;\n"
    "uniform vec2 texel;\n"
    "void main() {\n"
    "    vec3 c0 = (texture2D(texture0, fragTexCoord) * colDiffuse).rgb;\n"
    "    vec3 up1 = texture2D(texture0, fragTexCoord - vec2(0.0, texel.y)).rgb;\n"
    "    vec3 dn1 = texture2D(texture0, fragTexCoord + vec2(0.0, texel.y)).rgb;\n"
    "    vec3 up2 = texture2D(texture0, fragTexCoord - vec2(0.0, 2.0 * texel.y)).rgb;\n"
    "    vec3 dn2 = texture2D(texture0, fragTexCoord + vec2(0.0, 2.0 * texel.y)).rgb;\n"
    "    vec3 avg = (up1 + dn1 + up2 + dn2) * 0.25;\n"
    "    vec3 d = abs(c0 - avg);\n"
    "    float m = max(d.r, max(d.g, d.b));\n"
    "    float t = max(deband / 255.0, 1e-6);\n"
    "    float f = clamp(1.0 - m / t, 0.0, 1.0);\n"
    "    gl_FragColor = vec4(mix(c0, avg, f), 1.0);\n"
    "}\n";

// raylib GLSL330 fragment shader. Game-space anchored: gl_FragCoord is in
// drawable pixels, so the shader subtracts the image origin and divides by the
// on-screen scale (uGrid) before masking -- the periods are game-pixel-sized,
// not drawable-pixel-sized. colDiffuse is WHITE for DrawTexturePro, so the
// multiply is a no-op there but keeps the shader correct for tinted draws.
inline constexpr const char* kLcdShaderFragment330 =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float boost;\n"
    "uniform vec4 uGrid;\n"
    "uniform vec2 uGameSize;\n"
    "out vec4 finalColor;\n"
    "float lcdCoverage(float d, float w) {\n"
    "    if (!(w > 0.0)) return 0.0;\n"
    "    return clamp((w * 0.5 + 0.5 - d) / w, 0.0, 1.0);\n"
    "}\n"
    "float lcdDist(float x, float c, float p) {\n"
    "    return abs(mod(x - c + p * 0.5, p) - p * 0.5);\n"
    "}\n"
    "void main() {\n"
    "    vec4 tex = texture(texture0, fragTexCoord) * colDiffuse;\n"
    "    tex.rgb = clamp(tex.rgb * boost, 0.0, 1.0);\n"
    "    vec2 p = gl_FragCoord.xy - uGrid.xy;\n"
    "    vec2 ext = uGameSize * uGrid.zw;\n"
    "    if (!(p.x >= 0.0 && p.y >= 0.0 && p.x < ext.x && p.y < ext.y)) {\n"
    "        finalColor = tex;\n"
    "        return;\n"
    "    }\n"
    "    vec2 g = p / uGrid.zw;\n"
    "    vec3 stripe;\n"
    "    if (uGrid.z >= 3.0) {\n"
    "        float w = min(1.0, 3.0 / uGrid.z);\n"
    "        float f = g.x * 3.0;\n"
    "        stripe = vec3(0.6, 0.6, 1.0) * lcdCoverage(lcdDist(f, 0.5, 3.0), w)\n"
    "               + vec3(1.0, 0.6, 0.6) * lcdCoverage(lcdDist(f, 1.5, 3.0), w)\n"
    "               + vec3(0.6, 1.0, 0.6) * lcdCoverage(lcdDist(f, 2.5, 3.0), w);\n"
    "    } else {\n"
    "        float c = mod(floor(g.x), 3.0);\n"
    "        if (c < 1.0) stripe = vec3(0.6, 0.6, 1.0);\n"
    "        else if (c < 2.0) stripe = vec3(1.0, 0.6, 0.6);\n"
    "        else stripe = vec3(0.6, 1.0, 0.6);\n"
    "    }\n"
    "    float dark;\n"
    "    if (uGrid.w >= 1.0) {\n"
    "        float w = min(1.0, 1.0 / uGrid.w);\n"
    "        dark = lcdCoverage(lcdDist(g.y, 0.5, 2.0), w);\n"
    "    } else {\n"
    "        dark = mod(floor(g.y), 2.0) < 1.0 ? 1.0 : 0.0;\n"
    "    }\n"
    "    vec3 mask = mix(stripe, vec3(0.7), dark);\n"
    "    finalColor = vec4(tex.rgb * mask, tex.a);\n"
    "}\n";

// Standalone dither pass: the same Bayer step as its own program, so
// debanding works with the grid off. Indexed by GAME pixel (see
// lcd_dither_game) and confined to the presented image so the letterbox bars
// stay clean; the LCD pass (above, dither-free) shades its output when both
// are on.
inline constexpr const char* kDitherFragment330 =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float dither;\n"
    "uniform vec4 uGrid;\n"
    "uniform vec2 uGameSize;\n"
    "out vec4 finalColor;\n"
    "float bayer2(vec2 a) {\n"
    "    a = floor(a);\n"
    "    return fract(a.x * 0.5 + a.y * a.y * 0.75);\n"
    "}\n"
    "float bayer4(vec2 a) {\n"
    "    return bayer2(0.5 * a) * 0.25 + bayer2(a);\n"
    "}\n"
    "void main() {\n"
    "    vec4 tex = texture(texture0, fragTexCoord) * colDiffuse;\n"
    "    vec2 p = gl_FragCoord.xy - uGrid.xy;\n"
    "    vec2 ext = uGameSize * uGrid.zw;\n"
    "    if (p.x >= 0.0 && p.y >= 0.0 && p.x < ext.x && p.y < ext.y) {\n"
    "        vec2 g = p / uGrid.zw;\n"
    "        tex.rgb += (bayer4(mod(g, 4.0)) - 0.5) * dither * 8.0 / 255.0;\n"
    "    }\n"
    "    finalColor = tex;\n"
    "}\n";

// Standalone deband pass (game-texel space: bands are content strips, so the
// neighbors are game pixels, not screen pixels). Runs before dither/LCD.
inline constexpr const char* kDebandFragment330 =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float deband;\n"
    "uniform vec2 texel;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 c0 = (texture(texture0, fragTexCoord) * colDiffuse).rgb;\n"
    "    vec3 up1 = texture(texture0, fragTexCoord - vec2(0.0, texel.y)).rgb;\n"
    "    vec3 dn1 = texture(texture0, fragTexCoord + vec2(0.0, texel.y)).rgb;\n"
    "    vec3 up2 = texture(texture0, fragTexCoord - vec2(0.0, 2.0 * texel.y)).rgb;\n"
    "    vec3 dn2 = texture(texture0, fragTexCoord + vec2(0.0, 2.0 * texel.y)).rgb;\n"
    "    vec3 avg = (up1 + dn1 + up2 + dn2) * 0.25;\n"
    "    vec3 d = abs(c0 - avg);\n"
    "    float m = max(d.r, max(d.g, d.b));\n"
    "    float t = max(deband / 255.0, 1e-6);\n"
    "    float f = clamp(1.0 - m / t, 0.0, 1.0);\n"
    "    finalColor = vec4(mix(c0, avg, f), 1.0);\n"
    "}\n";

// Platform selector: desktop GLSL330, Android GLSL ES 1.00. The formulas are
// identical; only the dialect differs.
inline const char* lcd_shader_fragment_source() {
#if defined(__ANDROID__)
    return kLcdShaderFragmentES100;
#else
    return kLcdShaderFragment330;
#endif
}

inline const char* dither_fragment_source() {
#if defined(__ANDROID__)
    return kDitherFragmentES100;
#else
    return kDitherFragment330;
#endif
}

inline const char* deband_fragment_source() {
#if defined(__ANDROID__)
    return kDebandFragmentES100;
#else
    return kDebandFragment330;
#endif
}
}  // namespace gbarecomp
