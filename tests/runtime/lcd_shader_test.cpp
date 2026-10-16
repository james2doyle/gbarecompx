// LCD subpixel-grid shader policy + GLSL source. Pure, no display: the mask
// periods/levels must match the Shadertoy reference (ldBfDd) exactly, and the
// enablement resolution (config file vs env override) must give the env the
// final word.

#include "lcd_shader.h"

#include <cmath>
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

    // ---- the presented game grid ----------------------------------------
    // resolve_present_grid is the single translation from the drawable to the
    // frame: its outputs are the numbers every pass divides by, so they are
    // pinned as exact arithmetic (these are the measured cases the change was
    // written against).
    {
        auto grid_for = [](int dw, int dh, int bw = 240, int bh = 160) {
            return resolve_present_grid(compute_presentation_layout(dw, dh, bw, bh),
                                        bw, bh);
        };
        const PresentGrid g8 = grid_for(1920, 1280);
        check(g8.origin_x == 0 && g8.origin_y == 0 && g8.scale_x == 8.0f &&
                  g8.scale_y == 8.0f,
              "1920x1280 drawable is 8x at the origin");
        const PresentGrid g65 = grid_for(1440, 960);
        check(g65.scale_x == 6.0f, "1440x960 drawable is 6x");
        // 1080p fullscreen: the 3:2 image is 1620 wide inside 1920, so the
        // scale is fractional and the image is offset -- the case the old
        // screen-space grid got wrong twice over.
        const PresentGrid gf = grid_for(1920, 1080);
        check(gf.origin_x == 150 && gf.origin_y == 0, "1080p letterboxes 150px");
        check(std::fabs(gf.scale_x - 6.75f) < 1e-6f && gf.scale_y == 6.75f,
              "1080p scale is the fractional 6.75");
        const PresentGrid gp = grid_for(999, 666);
        check(std::fabs(gp.scale_x - 4.1625f) < 1e-6f,
              "an arbitrary window size resolves its own scale");
        const PresentGrid gtall = grid_for(1080, 1920);
        check(gtall.origin_x == 0 && gtall.origin_y == 600 &&
                  gtall.scale_x == 4.5f,
              "portrait drawable centres the image vertically");
        // Degenerate inputs must not divide by zero or poison the shaders.
        const PresentGrid gz = resolve_present_grid(PresentationLayout{}, 240, 160);
        check(gz.scale_x == 0.0f && gz.scale_y == 0.0f && gz.game_w == 240,
              "an empty layout yields a zero scale, not a NaN");
        const PresentGrid gz2 = grid_for(1920, 1280, 0, 0);
        check(gz2.scale_x == 0.0f && gz2.game_w == 0,
              "a zero logical size yields a zero scale");
        check(!grid_has_extent(gz) && grid_has_extent(g8),
              "extent is known only with both a size and a scale");
    }

    // ---- scale-1 parity ---------------------------------------------------
    // The game-space mask must reproduce the pinned Shadertoy table exactly at
    // 1:1. Sampled at pixel centres (gl_FragCoord's convention, where the old
    // `mod(gl_FragCoord.x, 3)` landed on the same stripe).
    {
        const PresentGrid unit{0, 0, 0, 0, 1.0f, 1.0f};
        int mismatches = 0;
        for (int x = -6; x <= 6; ++x)
            for (int y = -4; y <= 4; ++y) {
                const LcdMask want = lcd_pixel_mask(x, y);
                const LcdMask got =
                    lcd_pixel_mask_game(x + 0.5f, y + 0.5f, unit);
                if (want.r != got.r || want.g != got.g || want.b != got.b)
                    ++mismatches;
            }
        check(mismatches == 0,
              "1:1 game-space mask equals the pinned reference table");
    }

    // ---- one triad per game pixel ----------------------------------------
    // The property the change exists for: at every scale each game pixel must
    // split into blue/red/green thirds, in the reference's order, with the
    // stripe centres at 1/3 and 2/3 of the pixel. Dominance is the honest
    // test at fractional scales, where boundaries land mid-pixel.
    {
        auto dominant = [](const LcdMask& m) {
            if (m.b > m.r && m.b > m.g) return 2;  // blue stripe
            if (m.r > m.g) return 0;              // red stripe
            return 1;                              // green stripe
        };
        bool order = true, in_range = true;
        for (float sx : {3.0f, 4.0f, 6.0f, 8.0f, 9.0f, 12.0f, 6.75f, 4.1625f}) {
            const PresentGrid g{0, 0, 240, 160, sx, sx};
            const float y = 1.5f * sx;  // centre of an odd (striped) row
            for (int k = 0; k < 40; ++k) {
                // First third of pixel k ends blue, middle red, last green.
                order = order &&
                        dominant(lcd_pixel_mask_game(
                            (k + 1.0f / 6.0f) * sx, y, g)) == 2 &&
                        dominant(lcd_pixel_mask_game(
                            (k + 0.5f) * sx, y, g)) == 0 &&
                        dominant(lcd_pixel_mask_game(
                            (k + 5.0f / 6.0f) * sx, y, g)) == 1;
            }
            for (int i = 0; i < 200; ++i) {
                const LcdMask m = lcd_pixel_mask_game(
                    static_cast<float>(i) * 1.37f, y, g);
                in_range = in_range && m.r >= 0.0f && m.g >= 0.0f &&
                           m.b >= 0.0f && m.r <= 1.0f + 1e-5f &&
                           m.g <= 1.0f + 1e-5f && m.b <= 1.0f + 1e-5f;
            }
        }
        check(order,
              "every game pixel splits blue/red/green, in the reference order");
        check(in_range, "no mask sample exceeds the source range");
        // In stripe units the three centres are at 0.5/1.5/2.5 and the
        // boundaries between them at 1.0/2.0, which is exactly the thirds of
        // the game pixel (a triad is three stripe-widths wide, so stripe unit
        // 1.0 = one third of a pixel).
        bool centred = std::fabs(lcd_periodic_distance(0.5f, 0.5f, 3.0f)) <
                           1e-6f &&
                       std::fabs(lcd_periodic_distance(1.5f, 1.5f, 3.0f)) <
                           1e-6f &&
                       std::fabs(lcd_periodic_distance(2.5f, 2.5f, 3.0f)) <
                           1e-6f &&
                       std::fabs(lcd_periodic_distance(1.0f, 0.5f, 3.0f) -
                                 0.5f) < 1e-6f;
        check(centred, "stripe centres land on the thirds of the game pixel");
    }
    {
        // The exact pattern at 3x (one stripe per drawable pixel, so every
        // sample is pure): pixel k gets blue, red, green in thirds, and the
        // next pixel starts over.
        const PresentGrid g3{0, 0, 240, 160, 3.0f, 3.0f};
        const float y = 4.5f;
        bool order = true;
        order = order && mask_eq(lcd_pixel_mask_game(0.5f, y, g3), 0.6f, 0.6f,
                                 1.0f);
        order = order && mask_eq(lcd_pixel_mask_game(1.5f, y, g3), 1.0f, 0.6f,
                                 0.6f);
        order = order && mask_eq(lcd_pixel_mask_game(2.5f, y, g3), 0.6f, 1.0f,
                                 0.6f);
        order = order && mask_eq(lcd_pixel_mask_game(3.5f, y, g3), 0.6f, 0.6f,
                                 1.0f);
        check(order, "3x: pure blue/red/green thirds in the reference order");
    }

    // ---- sub-pixel collapse ----------------------------------------------
    // Under 3 drawable pixels per game pixel a triad cannot be drawn, so it
    // collapses to one stripe per game pixel -- the reference's own ratio.
    {
        bool collapsed = true;
        for (float sx : {1.0f, 1.5f, 2.0f, 2.99f}) {
            const PresentGrid g{0, 0, 240, 160, sx, sx};
            for (int k = 0; k < 12; ++k) {
                const LcdMask m = lcd_pixel_mask_game((k + 0.5f) * sx,
                                                      1.5f * sx, g);
                const int want = k % 3;
                const LcdMask expect =
                    want == 0 ? LcdMask{0.6f, 0.6f, 1.0f}
                              : want == 1 ? LcdMask{1.0f, 0.6f, 0.6f}
                                          : LcdMask{0.6f, 1.0f, 0.6f};
                collapsed = collapsed && mask_eq(m, expect.r, expect.g, expect.b);
            }
        }
        check(collapsed, "below 3x the triad collapses to one stripe per pixel");
    }

    // ---- coverage: sums to 1, never clips, reduces to the hard step ------
    {
        bool sums = true, in_range = true;
        for (float sx : {3.0f, 4.0f, 6.0f, 6.75f, 8.0f, 9.0f, 4.1625f}) {
            const PresentGrid g{0, 0, 240, 160, sx, sx};
            const float fw = std::min(1.0f, 3.0f / sx);
            for (int i = 0; i < 4000; ++i) {
                const float f = static_cast<float>(i) * 0.017f;  // game x * 3
                const float sum = lcd_stripe_coverage(
                                      lcd_periodic_distance(f, 0.5f, 3.0f), fw) +
                                  lcd_stripe_coverage(
                                      lcd_periodic_distance(f, 1.5f, 3.0f), fw) +
                                  lcd_stripe_coverage(
                                      lcd_periodic_distance(f, 2.5f, 3.0f), fw);
                sums = sums && std::fabs(sum - 1.0f) < 1e-4f;
                const LcdMask m = lcd_pixel_mask_game(f / 3.0f * sx, 0.5f, g);
                in_range = in_range && m.r >= 0.0f && m.g >= 0.0f &&
                           m.b >= 0.0f && m.r <= 1.0f + 1e-5f &&
                           m.g <= 1.0f + 1e-5f && m.b <= 1.0f + 1e-5f;
            }
        }
        check(sums, "the three stripe coverages sum to 1 (no net dimming)");
        check(in_range, "coverage-blended masks stay inside [0, 1]");
    }
    {
        // At 3x a stripe is exactly one drawable pixel, so every real fragment
        // (a pixel centre) is a pure stripe -- the anti-aliasing must not
        // soften the case where no softening is needed.
        const PresentGrid g3{0, 0, 240, 160, 3.0f, 3.0f};
        bool pure = true;
        for (int x = 0; x < 60; ++x) {
            const LcdMask m = lcd_pixel_mask_game(x + 0.5f, 4.5f, g3);
            const bool is_stripe = mask_eq(m, 0.6f, 0.6f, 1.0f) ||
                                   mask_eq(m, 1.0f, 0.6f, 0.6f) ||
                                   mask_eq(m, 0.6f, 1.0f, 0.6f);
            pure = pure && is_stripe;
        }
        check(pure, "3x: anti-aliasing reduces to the hard stripe");
    }

    // ---- rows: game rows, not drawable rows -------------------------------
    {
        const PresentGrid g8{0, 0, 240, 160, 8.0f, 8.0f};
        bool cadence = true;
        for (int row = 0; row < 4; ++row) {
            // Anywhere inside game row `row` the row decision must be constant.
            const LcdMask mid = lcd_pixel_mask_game(40.0f, row * 8.0f + 4.0f, g8);
            const LcdMask edge = lcd_pixel_mask_game(40.0f, row * 8.0f + 0.5f, g8);
            if (row % 2 == 0) {
                cadence = cadence && mask_eq(mid, 0.7f, 0.7f, 0.7f) &&
                          mask_eq(edge, 0.7f, 0.7f, 0.7f);
            } else {
                cadence = cadence && mid.g > mid.r && edge.g > edge.r;
            }
        }
        check(cadence,
              "8x: the dark banding follows GAME rows (8 drawable px each)");
        // The dark band edge sits on a game-row boundary, and a fragment
        // straddling it is partially darkened rather than snapped.
        const PresentGrid g65{0, 0, 240, 160, 6.75f, 6.75f};
        const LcdMask on_edge = lcd_pixel_mask_game(40.0f, 6.75f, g65);
        const LcdMask inside = lcd_pixel_mask_game(40.0f, 6.75f + 0.1f, g65);
        const LcdMask outside = lcd_pixel_mask_game(40.0f, 6.75f - 0.1f, g65);
        const float dark_on = (on_edge.r + on_edge.g + on_edge.b) / 3.0f;
        const float dark_in = (inside.r + inside.g + inside.b) / 3.0f;
        const float dark_out = (outside.r + outside.g + outside.b) / 3.0f;
        check(dark_out < dark_on && dark_on < dark_in,
              "a fragment on the row boundary is partially darkened");
    }

    // ---- origin anchoring and the letterbox exemption --------------------
    {
        // Same scale, image moved: the grid must move with it.
        const PresentGrid plain{0, 0, 240, 160, 8.0f, 8.0f};
        const PresentGrid offset{150, 30, 240, 160, 8.0f, 8.0f};
        const LcdMask a = lcd_pixel_mask_game(20.5f, 40.5f, plain);
        const LcdMask b = lcd_pixel_mask_game(170.5f, 70.5f, offset);
        check(mask_eq(b, a.r, a.g, a.b),
              "the grid is anchored to the image, not the screen");
        // Outside the image (all four sides, fractional extent included).
        const PresentGrid letterbox{150, 0, 240, 160, 6.75f, 6.75f};
        const LcdMask bare = LcdMask{1.0f, 1.0f, 1.0f};
        bool outside = true;
        for (float x : {-1.0f, 0.0f, 149.0f, 150.0f + 1620.0f, 1919.0f})
            for (float y : {-1.0f, 0.0f, 1079.0f})
                outside = outside &&
                          mask_eq(lcd_pixel_mask_game(x, y, letterbox), bare.r,
                                  bare.g, bare.b);
        check(outside, "letterbox bars are unshaded on every side");
        // ext.y = 160 * 6.75 = 1080 exactly, so row 1079 is the last row INSIDE the
        // image (1080p is a whole multiple in y and fractional only in x).
        check(lcd_inside_grid(200.0f, 10.0f, letterbox) &&
                  !lcd_inside_grid(100.0f, 10.0f, letterbox) &&
                  !lcd_inside_grid(200.0f, 1080.0f, letterbox) &&
                  lcd_inside_grid(200.0f, 1079.0f, letterbox),
              "inside/outside tracks the image rectangle");
        // A grid with no extent cannot exempt anything, so it masks all.
        const PresentGrid unit{0, 0, 0, 0, 1.0f, 1.0f};
        check(lcd_inside_grid(-50.0f, -50.0f, unit),
              "a grid with no extent masks the whole drawable");
    }

    // ---- axes are independent --------------------------------------------
    // A stretched grid (scale_x != scale_y) must use x's scale for the triad
    // width and y's for the row height -- the copy-paste that wires uGrid.z
    // into both would still look plausible here.
    {
        const PresentGrid g{0, 0, 240, 160, 6.0f, 9.0f};
        const LcdMask row_mid = lcd_pixel_mask_game(3.0f, 9.0f * 2.0f + 4.5f, g);
        check(mask_eq(row_mid, 0.7f, 0.7f, 0.7f),
              "the row axis uses the y scale (9px rows here)");
        // f = 2.5 (the green stripe centre) with scale_x = 6 -> gx = 5/6 -> x = 5.
        // Using the y scale here would put the sample off any stripe centre.
        // At 6x a stripe is 2px, so the fragment straddles its neighbours and
        // green wins on dominance rather than reaching 1.0 -- which is exactly
        // what the coverage blend is for.
        const LcdMask col_mid = lcd_pixel_mask_game(5.0f, 9.0f * 3.0f, g);
        check(col_mid.g > col_mid.r && col_mid.g > col_mid.b,
              "the column axis uses the x scale (6px pixels here)");
    }

    // ---- dither in game space --------------------------------------------
    {
        const PresentGrid g8{0, 0, 240, 160, 8.0f, 8.0f};
        // One cell per game pixel: every drawable pixel inside the same game
        // pixel must share the value. This is the property that separates
        // game-space dither from the old screen-space cell.
        // Game pixel (2, 2) occupies drawable pixels [16, 24) on each axis.
        bool block_constant = true;
        for (int py = 16; py < 24; ++py)
            for (int px = 16; px < 24; ++px) {
                const float want = lcd_dither_game(px + 0.5f, py + 0.5f, g8);
                for (int iy = 16; iy < 24; ++iy)
                    for (int ix = 16; ix < 24; ++ix)
                        block_constant =
                            block_constant &&
                            lcd_dither_game(ix + 0.5f, iy + 0.5f, g8) == want;
            }
        check(block_constant, "the dither cell is constant across a game pixel");
        check(lcd_dither_game(20.5f, 20.5f, g8) == lcd_bayer4(2, 2),
              "the cell is indexed by game pixel, not drawable pixel");
        // Mean preserved over a 4x4 game-pixel cell: the same exact -4 the
        // screen-space test pins, from the STEPS (not the applied channels --
        // summing applied channels would fold in the 16 * 0.5 base). Sampled
        // at the centre of each game pixel on an 8x drawable.
        float sum = 0.0f;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) {
                const float v = lcd_dither_game((x + 0.5f) * 8.0f,
                                               (y + 0.5f) * 8.0f, g8);
                sum += (v - 0.5f) * kLcdSourceStepLSB;
            }
        check(sum == -4.0f,
              "the game-space cell keeps the known half-step (mean intact)");
        // 1:1 parity with the old screen-space index.
        const PresentGrid unit{0, 0, 0, 0, 1.0f, 1.0f};
        bool parity = true;
        for (int y = -4; y < 5; ++y)
            for (int x = -4; x < 5; ++x)
                parity = parity && lcd_dither_game(x + 0.5f, y + 0.5f, unit) ==
                                         lcd_bayer4(x, y);
        check(parity, "at 1:1 the dither index is the old screen-space index");
        // Bars get no grain.
        const PresentGrid letterbox{150, 0, 240, 160, 6.75f, 6.75f};
        check(lcd_dither_step_game(0.5f, 10.0f, 10.0f, letterbox, 1.0f) == 0.5f,
              "dither leaves the letterbox untouched");
        check(lcd_dither_step_game(0.5f, 200.0f, 10.0f, letterbox, 1.0f) !=
                  0.5f,
              "dither does perturb the image");
    }

    // ---- deband's step is one game pixel, whatever the scale -------------
    {
        const TexelStep s = shader_deband_texel_step(240, 160);
        bool one_texel = std::fabs(s.x * 240.0f - 1.0f) < 1e-6f &&
                         std::fabs(s.y * 160.0f - 1.0f) < 1e-6f;
        // Through the sharp pre-upscale target the same UV step spans
        // `factor` texels -- still exactly one game pixel.
        for (int factor : {2, 3, 4, 5})
            one_texel = one_texel &&
                        std::fabs(s.x * (240 * factor) - factor) < 1e-4f &&
                        std::fabs(s.y * (160 * factor) - factor) < 1e-4f;
        check(one_texel,
              "the deband UV step is one game pixel on both source paths");
        const TexelStep z = shader_deband_texel_step(0, 0);
        check(z.x == 0.0f && z.y == 0.0f,
              "a zero logical size yields a zero step, not a NaN");
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
    // GL 3.3 context), raylib's sampler/uniform names, the game-grid uniforms
    // every scale-aware pass needs, the two reference levels, and the boost
    // uniform the backend sets.
    check(std::strstr(kLcdShaderFragment330, "#version 330") != nullptr,
          "GLSL targets 330");
    check(std::strstr(kLcdShaderFragment330, "texture0") != nullptr,
          "GLSL samples raylib texture0");
    check(std::strstr(kLcdShaderFragment330, "gl_FragCoord") != nullptr,
          "GLSL derives the grid from drawable pixels");
    check(std::strstr(kLcdShaderFragment330, "uniform float boost") != nullptr,
          "GLSL exposes the boost uniform");
    check(std::strstr(kLcdShaderFragment330, "bayer") == nullptr,
          "LCD program carries no dither (standalone pass)");
    check(std::strstr(kDitherFragment330, "bayer4(mod(g, 4.0))") != nullptr,
          "dither program indexes the cell by game pixel");
    check(std::strstr(kDitherFragment330, "uniform float dither") != nullptr,
          "dither program exposes its strength");
    check(std::strstr(kDebandFragment330, "uniform float deband") != nullptr,
          "deband program exposes its threshold");
    check(std::strstr(kDebandFragment330, "uniform vec2 texel") != nullptr,
          "deband program takes the texel step");
    check(std::strstr(kLcdShaderFragment330, "finalColor") != nullptr,
          "GLSL writes finalColor");

    // The grid uniforms are the whole point of the change: without them the
    // periods are drawable pixels and the mask drifts off the game grid. Both
    // scale-aware programs must declare and USE them (a declared-but-unused
    // uniform would be optimised out and the location lookup would fail).
    {
        struct Prog {
            const char* src;
            const char* name;
        };
        const Prog grid_programs[] = {
            {kLcdShaderFragment330, "LCD 330"},
            {kDitherFragment330, "dither 330"},
            {kLcdShaderFragmentES100, "LCD ES100"},
            {kDitherFragmentES100, "dither ES100"},
        };
        bool declared = true, used = true;
        for (const Prog& prog : grid_programs) {
            declared = declared && std::strstr(prog.src, "uniform vec4 uGrid") &&
                       std::strstr(prog.src, "uniform vec2 uGameSize");
            // Origin, then the scale as a vec2 (game coords = (p - origin) /
            // scale). The LCD program additionally thresholds each axis
            // separately -- uGrid.z for the triad width, uGrid.w for the row
            // height -- which is where a copy-paste between axes would hide.
            used = used && std::strstr(prog.src, "gl_FragCoord.xy - uGrid.xy") &&
                   std::strstr(prog.src, "uGrid.zw");
            if (std::strstr(prog.src, "lcdCoverage")) {
                used = used && std::strstr(prog.src, "uGrid.z >= 3.0") &&
                       std::strstr(prog.src, "uGrid.w >= 1.0");
            }
        }
        check(declared,
              "grid and game-size uniforms declared in both dialects");
        check(used,
              "every grid program reads origin and scale; LCD splits the axes");
    }
    // The bars exemption: the early-out must exist in the shader, not just in
    // the CPU mirror, or the letterbox gets striped/dithered again.
    check(std::strstr(kLcdShaderFragment330, "p.x < ext.x") != nullptr,
          "LCD pass exempts fragments outside the image");
    check(std::strstr(kDitherFragment330, "p.x < ext.x") != nullptr,
          "dither pass exempts fragments outside the image");

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
    check(std::strstr(kDitherFragmentES100, "bayer4(mod(g, 4.0))") !=
                  nullptr,
          "ES dither indexes the cell by game pixel");
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
