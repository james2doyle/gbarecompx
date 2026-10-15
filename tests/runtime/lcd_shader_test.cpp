// LCD subpixel-grid shader policy + GLSL source. Pure, no display: the mask
// periods/levels must match the Shadertoy reference (ldBfDd) exactly, and the
// enablement resolution (config file vs env override) must give the env the
// final word.

#include "lcd_shader.h"

#include <cstdio>
#include <cstring>
#include <limits>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (ok) {
        std::printf("  ok    %s\n", what);
    } else {
        std::printf("  FAIL  %s\n", what);
        ++failures;
    }
}

bool mask_eq(gbarecomp::LcdMask m, float r, float g, float b) {
    return m.r == r && m.g == g && m.b == b;
}

}  // namespace

int main() {
    using namespace gbarecomp;

    // Defaults are lifted from the reference (pb = 0.4, sclV = 0.25) for
    // brightness — see LcdShaderParams: gain clips, floors do not.
    {
        LcdShaderParams d;
        check(d.base_level == 0.6f, "default base lifts the reference pb");
        check(d.dark_row_level == 0.7f,
              "default dark row lifts the reference sclV");
        check(kLcdDefaultBoost == 1.0f, "default boost is unity (no clipping)");
    }

    // Odd rows carry the subpixel columns: x = 0 mod 3 -> blue,
    // 1 -> red, 2 -> green, then the period repeats.
    check(mask_eq(lcd_pixel_mask(0, 1), 0.6f, 0.6f, 1.0f),
          "x=0 column is blue");
    check(mask_eq(lcd_pixel_mask(1, 1), 1.0f, 0.6f, 0.6f),
          "x=1 column is red");
    check(mask_eq(lcd_pixel_mask(2, 1), 0.6f, 1.0f, 0.6f),
          "x=2 column is green");
    check(mask_eq(lcd_pixel_mask(3, 1), 0.6f, 0.6f, 1.0f),
          "mask repeats every 3 pixels");
    check(mask_eq(lcd_pixel_mask(7, 1), 1.0f, 0.6f, 0.6f),
          "mask phase holds at x=7");

    // Even rows are flat dark whatever the column.
    {
        bool flat = true;
        for (int x = 0; x < 6; ++x)
            flat = flat &&
                   mask_eq(lcd_pixel_mask(x, 0), 0.7f, 0.7f, 0.7f);
        check(flat, "even rows are flat dark across columns");
    }
    check(mask_eq(lcd_pixel_mask(1, 4), 0.7f, 0.7f, 0.7f),
          "even rows are flat dark at y=4");

    // Negative coords wrap instead of producing a garbage branch: x = -1 is
    // 2 mod 3 (green), y = -1 is odd (masked, not flat).
    check(mask_eq(lcd_pixel_mask(-1, -1), 0.6f, 1.0f, 0.6f),
          "negative coords wrap to the right column");
    check(mask_eq(lcd_pixel_mask(0, -2), 0.7f, 0.7f, 0.7f),
          "negative even rows are flat dark");

    // Custom levels flow through both paths.
    {
        LcdShaderParams p{0.5f, 0.1f};
        check(mask_eq(lcd_pixel_mask(1, 1, p), 1.0f, 0.5f, 0.5f),
              "custom base level reaches the mask");
        check(mask_eq(lcd_pixel_mask(1, 0, p), 0.1f, 0.1f, 0.1f),
              "custom dark level reaches even rows");
    }

    // Env convention mirrors GBARECOMP_TOUCH_CONTROLS: set + not "0" is on.
    check(!lcd_shader_env_on(nullptr), "null env is off");
    check(!lcd_shader_env_on(""), "empty env is off");
    check(!lcd_shader_env_on("0"), "env 0 is off");
    check(lcd_shader_env_on("1"), "env 1 is on");
    check(!lcd_shader_env_set(nullptr), "null env is unset");
    check(!lcd_shader_env_set(""), "empty env is unset");
    check(lcd_shader_env_set("0"), "env 0 counts as set (overrides config)");

    // [video] line parsing: case-insensitive key and values, unknown keys
    // declined, garbage values claim the key but change nothing.
    {
        bool v = false;
        check(apply_lcd_shader_line("lcd_shader", "1", &v) && v,
              "lcd_shader=1 parses on");
        v = true;
        check(apply_lcd_shader_line("LCD_SHADER", "off", &v) && !v,
              "LCD_SHADER=off parses off, case-insensitive");
        v = false;
        check(apply_lcd_shader_line("lcd_shader", "True", &v) && v,
              "True parses on");
        v = true;
        check(apply_lcd_shader_line("lcd_shader", "maybe", &v) && v,
              "garbage claims the key but changes nothing");
        v = false;
        check(!apply_lcd_shader_line("screen", "1", &v) && !v,
              "neighbour keys declined");
        check(!apply_lcd_shader_line(nullptr, "1", &v),
              "null key declined");
        check(!apply_lcd_shader_line("lcd_shader", "1", nullptr),
              "null out declined");
    }

    // Resolution: env wins when set (either direction), else the config.
    check(!resolve_lcd_shader(false, nullptr), "no env, config off -> off");
    check(resolve_lcd_shader(true, nullptr), "no env, config on -> on");
    check(resolve_lcd_shader(false, "1"), "env 1 beats config off");
    check(!resolve_lcd_shader(true, "0"), "env 0 beats config on");
    check(!resolve_lcd_shader(false, ""), "empty env defers to config");

    // Boost: NaN-safe clamp to [1, 3], per-channel apply with a 1.0 ceiling.
    check(lcd_clamp_boost(1.5f) == 1.5f, "in-range boost passes through");
    check(lcd_clamp_boost(0.2f) == 1.0f, "low boost floors at 1.0");
    check(lcd_clamp_boost(9.0f) == 3.0f, "high boost caps at 3.0");
    check(lcd_clamp_boost(std::numeric_limits<float>::quiet_NaN()) == 1.0f,
          "NaN boost floors at 1.0");
    check(lcd_apply_boost(0.5f, 1.5f) == 0.75f, "mids scale up");
    check(lcd_apply_boost(0.8f, 2.0f) == 1.0f, "overflow clips, not wraps");
    check(lcd_apply_boost(0.0f, 3.0f) == 0.0f, "black stays black");

    // Boost config line: case-insensitive key, numeric value, garbage claims
    // the key but changes nothing, neighbours declined.
    {
        float b = 1.0f;
        check(apply_lcd_boost_line("lcd_boost", "2.0", &b) && b == 2.0f,
              "lcd_boost=2.0 parses");
        b = 1.0f;
        check(apply_lcd_boost_line("LCD_BOOST", "0.5", &b) && b == 1.0f,
              "sub-1.0 boost clamps, case-insensitive");
        b = 1.0f;
        check(apply_lcd_boost_line("lcd_boost", "99", &b) && b == 3.0f,
              "huge boost caps at 3.0");
        b = 2.0f;
        check(apply_lcd_boost_line("lcd_boost", "maybe", &b) && b == 2.0f,
              "garbage boost leaves the value alone");
        b = 1.0f;
        check(!apply_lcd_boost_line("lcd_shader", "2.0", &b) && b == 1.0f,
              "boost reader declines neighbour keys");
    }

    // Bayer dither: 16 distinct levels over each 4x4 cell, periodic, centred.
    {
        float seen[16];
        for (int i = 0; i < 16; ++i) seen[i] = -1.0f;
        bool distinct = true;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) {
                const float v = lcd_bayer4(x, y);
                if (!(v >= 0.0f && v < 1.0f)) distinct = false;
                for (int i = 0; i < 16; ++i)
                    if (seen[i] == v) distinct = false;
                seen[y * 4 + x] = v;
            }
        check(distinct, "bayer cell holds 16 distinct levels in [0, 1)");
        check(lcd_bayer4(0, 0) == 0.0f, "bayer origin is zero");
        check(lcd_bayer4(4, 0) == lcd_bayer4(0, 0) &&
                  lcd_bayer4(0, 4) == lcd_bayer4(0, 0) &&
                  lcd_bayer4(7, 5) == lcd_bayer4(3, 1),
              "bayer repeats every 4 pixels");
        // Mean-centred up to the half-step a 16-level matrix leaves, scaled by
        // the source step: the values are exactly {0..15}/16, so the summed
        // step is exactly -0.5 * 8.
        float sum = 0.0f;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                sum += lcd_dither_step(x, y, 1.0f);
        check(sum == -4.0f, "dither steps sum to the known half-step");
        check(lcd_apply_dither(0.5f, 0, 0, 1.0f) < 0.5f,
              "dither perturbs a mid channel");
        int hx = 0, hy = 0;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                if (lcd_bayer4(x, y) > lcd_bayer4(hx, hy)) {
                    hx = x;
                    hy = y;
                }
        check(lcd_apply_dither(1.0f, hx, hy, 2.0f) == 1.0f,
              "dither never exceeds white");
        check(lcd_apply_dither(0.0f, 0, 0, 2.0f) == 0.0f,
              "dither never goes below black");
        check(kLcdDefaultDither == 1.0f, "dither defaults on at unity");
        check(lcd_clamp_dither(-1.0f) == 0.0f, "negative dither clamps to 0");
        check(lcd_clamp_dither(9.0f) == 2.0f, "huge dither caps at 2.0");
        check(lcd_clamp_dither(
                   std::numeric_limits<float>::quiet_NaN()) == 0.0f,
              "NaN dither clamps to 0");
    }

    // Dither config line.
    {
        float d = 1.0f;
        check(apply_lcd_dither_line("lcd_dither", "0", &d) && d == 0.0f,
              "lcd_dither=0 disables");
        d = 1.0f;
        check(apply_lcd_dither_line("LCD_DITHER", "2", &d) && d == 2.0f,
              "dither parses, case-insensitive");
        d = 1.0f;
        check(apply_lcd_dither_line("lcd_dither", "maybe", &d) && d == 1.0f,
              "garbage dither leaves the value alone");
        d = 1.0f;
        check(!apply_lcd_dither_line("lcd_boost", "0", &d) && d == 1.0f,
              "dither reader declines neighbour keys");
    }

    // Deband mix: flat ground blends fully, hard edges pass through, the
    // ramp between is linear. Mirrors the GLSL mix toward the neighbor mean.
    {
        check(lcd_deband_factor(0.0f, 0.02f) == 1.0f,
              "flat ground blends fully");
        check(lcd_deband_factor(0.5f, 0.02f) == 0.0f,
              "hard edges pass through");
        const float mid = lcd_deband_factor(0.01f, 0.02f);
        check(mid > 0.49f && mid < 0.51f, "midpoint blends half");
        check(lcd_deband_factor(0.0f, 0.0f) == 0.0f,
              "zero threshold keeps the source");
        check(lcd_deband_factor(1.0f, 0.0f) == 0.0f,
              "zero threshold never blends");
        check(lcd_deband_maxdiff(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f) == 0.0f,
              "identical pixels differ by zero");
        check(lcd_deband_maxdiff(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f) == 1.0f,
              "worst channel wins the max");
        check(kLcdDefaultDeband == 0.0f, "deband defaults off");
        check(lcd_clamp_deband(-1.0f) == 0.0f, "negative deband clamps to 0");
        check(lcd_clamp_deband(99.0f) == 16.0f, "huge deband caps at 16");
        check(lcd_clamp_deband(
                   std::numeric_limits<float>::quiet_NaN()) == 0.0f,
              "NaN deband clamps to 0");
    }

    // Deband config line.
    {
        float d = 0.0f;
        check(apply_lcd_deband_line("lcd_deband", "6", &d) && d == 6.0f,
              "lcd_deband=6 parses");
        d = 0.0f;
        check(apply_lcd_deband_line("LCD_DEBAND", "99", &d) && d == 16.0f,
              "huge deband caps, case-insensitive");
        d = 6.0f;
        check(apply_lcd_deband_line("lcd_deband", "maybe", &d) && d == 6.0f,
              "garbage deband leaves the value alone");
        d = 0.0f;
        check(!apply_lcd_deband_line("lcd_dither", "6", &d) && d == 0.0f,
              "deband reader declines neighbour keys");
    }

    // The GLSL must carry what the backend relies on: a 330 version (desktop
    // GL 3.3 context), raylib's sampler/uniform names, screen-pixel coords,
    // the two reference levels, and the boost uniform the backend sets.
    check(std::strstr(kLcdShaderFragment330, "#version 330") != nullptr,
          "GLSL targets 330");
    check(std::strstr(kLcdShaderFragment330, "texture0") != nullptr,
          "GLSL samples raylib texture0");
    check(std::strstr(kLcdShaderFragment330, "gl_FragCoord") != nullptr,
          "GLSL masks in screen pixels");
    check(std::strstr(kLcdShaderFragment330, "uniform float boost") != nullptr,
          "GLSL exposes the boost uniform");
    check(std::strstr(kLcdShaderFragment330, "bayer") == nullptr,
          "LCD program carries no dither (standalone pass)");
    check(std::strstr(kDitherFragment330, "bayer4(gl_FragCoord.xy)") !=
                  nullptr,
          "dither program works in screen pixels");
    check(std::strstr(kDitherFragment330, "uniform float dither") != nullptr,
          "dither program exposes its strength");
    check(std::strstr(kDebandFragment330, "uniform float deband") != nullptr,
          "deband program exposes its threshold");
    check(std::strstr(kDebandFragment330, "uniform vec2 texel") != nullptr,
          "deband program takes the texel step");
    check(std::strstr(kLcdShaderFragment330, "finalColor") != nullptr,
          "GLSL writes finalColor");

    // GLSL ES 1.00 twins for Android: same formulas, ES dialect (precision,
    // varying, texture2D, gl_FragColor). highp, not mediump: the dither hash
    // squares gl_FragCoord, which overflows mediump's 65504 max.
    check(std::strstr(kLcdShaderFragmentES100, "#version 100") != nullptr,
          "ES LCD targets 100");
    check(std::strstr(kLcdShaderFragmentES100, "precision highp float") !=
                  nullptr,
          "ES LCD uses highp precision");
    check(std::strstr(kLcdShaderFragmentES100, "texture2D") != nullptr,
          "ES LCD samples with texture2D");
    check(std::strstr(kLcdShaderFragmentES100, "gl_FragColor") != nullptr,
          "ES LCD writes gl_FragColor");
    check(std::strstr(kLcdShaderFragmentES100, "uniform float boost") !=
                  nullptr,
          "ES LCD exposes the boost uniform");
    check(std::strstr(kLcdShaderFragmentES100, "0.6, 0.6, 1.0") != nullptr,
          "ES LCD keeps the triad levels");
    check(std::strstr(kDitherFragmentES100, "bayer4(gl_FragCoord.xy)") !=
                  nullptr,
          "ES dither keeps the screen-space hash");
    check(std::strstr(kDitherFragmentES100, "uniform float dither") !=
                  nullptr,
          "ES dither exposes its strength");
    check(std::strstr(kDebandFragmentES100, "uniform vec2 texel") != nullptr,
          "ES deband takes the texel step");
    check(std::strstr(kDebandFragmentES100, "texture2D") != nullptr &&
                  std::strstr(kDebandFragmentES100, "texture(") == nullptr,
          "ES deband uses texture2D throughout");
    // The selector serves the dialect matching the build target.
#if defined(__ANDROID__)
    check(lcd_shader_fragment_source() == kLcdShaderFragmentES100,
          "Android selects the ES LCD source");
    check(dither_fragment_source() == kDitherFragmentES100,
          "Android selects the ES dither source");
    check(deband_fragment_source() == kDebandFragmentES100,
          "Android selects the ES deband source");
#else
    check(lcd_shader_fragment_source() == kLcdShaderFragment330,
          "desktop selects the 330 LCD source");
    check(dither_fragment_source() == kDitherFragment330,
          "desktop selects the 330 dither source");
    check(deband_fragment_source() == kDebandFragment330,
          "desktop selects the 330 deband source");
#endif

    std::printf("lcd_shader tests: %s (%d failure(s))\n",
                failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
