#pragma once

#include <cmath>
#include <cstdio>

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

// Per-screen-pixel multiplier. Even rows (y mod 2 == 0) are flat dark;
// odd rows carry the RGB subpixel column.
inline LcdMask lcd_pixel_mask(int screen_x, int screen_y,
                              LcdShaderParams p = LcdShaderParams{}) {
    if (lcd_floor_mod(screen_y, 2) == 0)
        return LcdMask{p.dark_row_level, p.dark_row_level, p.dark_row_level};
    switch (lcd_floor_mod(screen_x, 3)) {
        case 1: return LcdMask{1.0f, p.base_level, p.base_level};
        case 2: return LcdMask{p.base_level, 1.0f, p.base_level};
        default: return LcdMask{p.base_level, p.base_level, 1.0f};
    }
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
// screen space, where the bands are visible. It feeds the framebuffer's own
// 8-bit quantization — the final rounding step.
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
inline constexpr const char* kLcdShaderFragmentES100 =
    "#version 100\n"
    "precision highp float;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float boost;\n"
    "void main() {\n"
    "    vec4 tex = texture2D(texture0, fragTexCoord) * colDiffuse;\n"
    "    tex.rgb = clamp(tex.rgb * boost, 0.0, 1.0);\n"
    "    vec3 mask;\n"
    "    float mx = mod(gl_FragCoord.x, 3.0);\n"
    "    if (mx < 1.0) mask = vec3(0.6, 0.6, 1.0);\n"
    "    else if (mx < 2.0) mask = vec3(1.0, 0.6, 0.6);\n"
    "    else mask = vec3(0.6, 1.0, 0.6);\n"
    "    if (mod(gl_FragCoord.y, 2.0) < 1.0) mask = vec3(0.7);\n"
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
    "float bayer2(vec2 a) {\n"
    "    a = floor(a);\n"
    "    return fract(a.x * 0.5 + a.y * a.y * 0.75);\n"
    "}\n"
    "float bayer4(vec2 a) {\n"
    "    return bayer2(0.5 * a) * 0.25 + bayer2(a);\n"
    "}\n"
    "void main() {\n"
    "    vec4 tex = texture2D(texture0, fragTexCoord) * colDiffuse;\n"
    "    tex.rgb += (bayer4(gl_FragCoord.xy) - 0.5) * dither * 8.0 / 255.0;\n"
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

// raylib GLSL330 fragment shader. Screen-space: gl_FragCoord is in drawable
// pixels, so the 3-pixel / 2-row periods land on physical pixels exactly as
// the reference's FRAGCOORD does. colDiffuse is WHITE for DrawTexturePro, so
// the multiply is a no-op there but keeps the shader correct for tinted draws.
inline constexpr const char* kLcdShaderFragment330 =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float boost;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 tex = texture(texture0, fragTexCoord) * colDiffuse;\n"
    "    tex.rgb = clamp(tex.rgb * boost, 0.0, 1.0);\n"
    "    vec3 mask;\n"
    "    float mx = mod(gl_FragCoord.x, 3.0);\n"
    "    if (mx < 1.0) mask = vec3(0.6, 0.6, 1.0);\n"
    "    else if (mx < 2.0) mask = vec3(1.0, 0.6, 0.6);\n"
    "    else mask = vec3(0.6, 1.0, 0.6);\n"
    "    if (mod(gl_FragCoord.y, 2.0) < 1.0) mask = vec3(0.7);\n"
    "    finalColor = vec4(tex.rgb * mask, tex.a);\n"
    "}\n";

// Standalone dither pass: the same Bayer step as its own program, so
// debanding works with the grid off. Runs first in screen space; the LCD
// pass (above, dither-free) shades its output when both are on.
inline constexpr const char* kDitherFragment330 =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float dither;\n"
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
    "    tex.rgb += (bayer4(gl_FragCoord.xy) - 0.5) * dither * 8.0 / 255.0;\n"
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
