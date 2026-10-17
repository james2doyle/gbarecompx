// MMPX edge smoothing — style-preserving pixel-art magnification (2×).
//
// Algorithm: McGuire & Gagiu, "MMPX Style-Preserving Pixel Art
// Magnification", JCGT 2021 (jcgt.org/published/0010/02/04, open access).
// MIT-licensed reference implementation ported for tests:
// tests/runtime/support/mmpx_reference_c99.c (ITotalJustice's C99 form of
// the authors' code — the differential oracle). Do not paste libretro's
// slang into the game sources (dialect + hygiene); this header is our own
// transliteration, pinned against the reference by tests.
//
// Shape of this header (the lcd_shader.h pattern): a CPU mirror of the
// rule cascade — the readable spec — plus the GLSL transliteration in both
// dialects (GLSL 330 desktop, GLSL ES 1.00 Android). The mirror is
// differential-tested byte-for-byte against the vendored reference; the
// GLSL is pinned structurally to the mirror and validated with
// glslangValidator at dev time.
//
// Semantics worth stating up front:
//  * Every output pixel equals SOME source pixel's value — the palette is
//    preserved exactly, no blends. Dither patterns and 1px features pass
//    through as content (MMPX does not "de-dither"; that is HQx/AA-xBR's
//    jurisdiction, and it is not what we want for FFTA's hand-dithered
//    GBA gradients).
//  * Straight vertical/horizontal edges and perfect 45° staircases pass
//    through unchanged (each source pixel's 2×2 block stays uniform). The
//    reconstruction lives on 2:1 slopes, intersections, and corners.
//  * The reference compares packed RGBA (alpha included) and weights luma
//    by (256 − alpha). FFTA's framebuffer is opaque (alpha 255), so the
//    GLSL assumes opaque input: equality is RGB-only and luma drops the
//    alpha factor. The mirror stays alpha-faithful; on opaque input the
//    two agree exactly (differential test runs opaque data).
//  * Neighborhood taps clamp to the border (border replication), exactly
//    like the reference's src(). The GLSL clamps manually — the game
//    texture's wrap state is raylib's default REPEAT, so an unclamped tap
//    at an image edge would wrap to the opposite edge and corrupt every
//    rule there.
//
// Integration (raylib_backend.cpp): the pass runs FIRST in the present
// chain — the game texture is magnified 2× into its own render target, and
// the existing chain (sharp prescale, deband, dither, LCD grid) consumes
// that stage. MMPX changes WHAT is scaled; sharp/finish change HOW.
// Deband's 1-game-px texel step spans exactly 2 MMPX texels (the property
// its comment already documents for the sharp target); the dither and the
// LCD grid index gl_FragCoord in game space and are texel-density-
// agnostic.
#pragma once

#include <cstdint>
#include <cstring>

namespace gbarecomp::mmpx {

// ── the CPU mirror (the spec) ────────────────────────────────────────────
// Packed RGBA8888 (r in the low byte, matching the reference). Dst must be
// (2w × 2h). Degenerate sizes return without touching dst.

inline std::uint32_t luma(std::uint32_t color) {
    const std::uint32_t alpha = (color & 0xFF000000u) >> 24;
    return (((color & 0x00FF0000u) >> 16) + ((color & 0x0000FF00u) >> 8) +
            (color & 0x000000FFu) + 1) *
           (256 - alpha);
}

inline bool all_eq2(std::uint32_t b, std::uint32_t a0, std::uint32_t a1) {
    return ((b ^ a0) | (b ^ a1)) == 0;
}

inline bool all_eq3(std::uint32_t b, std::uint32_t a0, std::uint32_t a1,
                    std::uint32_t a2) {
    return ((b ^ a0) | (b ^ a1) | (b ^ a2)) == 0;
}

inline bool all_eq4(std::uint32_t b, std::uint32_t a0, std::uint32_t a1,
                    std::uint32_t a2, std::uint32_t a3) {
    return ((b ^ a0) | (b ^ a1) | (b ^ a2) | (b ^ a3)) == 0;
}

inline bool any_eq3(std::uint32_t b, std::uint32_t a0, std::uint32_t a1,
                    std::uint32_t a2) {
    return b == a0 || b == a1 || b == a2;
}

inline bool none_eq2(std::uint32_t b, std::uint32_t a0, std::uint32_t a1) {
    return b != a0 && b != a1;
}

inline bool none_eq4(std::uint32_t b, std::uint32_t a0, std::uint32_t a1,
                     std::uint32_t a2, std::uint32_t a3) {
    return b != a0 && b != a1 && b != a2 && b != a3;
}

// Border-replicating tap, same clamp trick as the reference's src(): a
// negative coordinate casts to a huge unsigned and clamps.
inline std::uint32_t src_px(const std::uint32_t* buf, int w, int h, int x,
                            int y) {
    const int max_x = w - 1, max_y = h - 1;
    if (static_cast<std::uint32_t>(x) > static_cast<std::uint32_t>(max_x) ||
        static_cast<std::uint32_t>(y) > static_cast<std::uint32_t>(max_y)) {
        x = x < 0 ? 0 : (x > max_x ? max_x : x);
        y = y < 0 ? 0 : (y > max_y ? max_y : y);
    }
    return buf[y * w + x];
}

// The cascade. Rule GROUPS run in the reference's order (1:1 slopes,
// intersections, 2:1 slopes); later rules observe earlier assignments
// (the 2:1 rules copy quadrant values set by earlier ones). Within a
// group, rules fire sequentially and may override one another — the order
// is part of the algorithm, keep it verbatim.
inline void scale2x(const std::uint32_t* src, std::uint32_t* dst, int w,
                    int h) {
    if (!src || !dst || w <= 0 || h <= 0) return;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::uint32_t A = src_px(src, w, h, x - 1, y - 1);
            const std::uint32_t B = src_px(src, w, h, x, y - 1);
            const std::uint32_t C = src_px(src, w, h, x + 1, y - 1);
            const std::uint32_t D = src_px(src, w, h, x - 1, y);
            const std::uint32_t E = src_px(src, w, h, x, y);
            const std::uint32_t F = src_px(src, w, h, x + 1, y);
            const std::uint32_t G = src_px(src, w, h, x - 1, y + 1);
            const std::uint32_t H = src_px(src, w, h, x, y + 1);
            const std::uint32_t I = src_px(src, w, h, x + 1, y + 1);

            std::uint32_t J = E, K = E, L = E, M = E;

            if (((A ^ E) | (B ^ E) | (C ^ E) | (D ^ E) | (F ^ E) | (G ^ E) |
                 (H ^ E) | (I ^ E)) != 0) {
                const std::uint32_t P = src_px(src, w, h, x, y - 2);
                const std::uint32_t S = src_px(src, w, h, x, y + 2);
                const std::uint32_t Q = src_px(src, w, h, x - 2, y);
                const std::uint32_t R = src_px(src, w, h, x + 2, y);
                const std::uint32_t Bl = luma(B), Dl = luma(D);
                const std::uint32_t El = luma(E), Fl = luma(F);
                const std::uint32_t Hl = luma(H);

                // 1:1 slope rules
                {
                    if ((D == B && D != H && D != F) &&
                        (El >= Dl || E == A) && any_eq3(E, A, C, G) &&
                        ((El < Dl) || A != D || E != P || E != Q))
                        J = D;
                    if ((B == F && B != D && B != H) &&
                        (El >= Bl || E == C) && any_eq3(E, A, C, I) &&
                        ((El < Bl) || C != B || E != P || E != R))
                        K = B;
                    if ((H == D && H != F && H != B) &&
                        (El >= Hl || E == G) && any_eq3(E, A, G, I) &&
                        ((El < Hl) || G != H || E != S || E != Q))
                        L = H;
                    if ((F == H && F != B && F != D) &&
                        (El >= Fl || E == I) && any_eq3(E, C, G, I) &&
                        ((El < Fl) || I != H || E != R || E != S))
                        M = F;
                }

                // Intersection rules
                {
                    const std::uint32_t F3 = src_px(src, w, h, x + 3, y);
                    const std::uint32_t D3 = src_px(src, w, h, x - 3, y);
                    const std::uint32_t H3 = src_px(src, w, h, x, y + 3);
                    const std::uint32_t B3 = src_px(src, w, h, x, y - 3);
                    if ((E != F && all_eq4(E, C, I, D, Q) &&
                         all_eq2(F, B, H)) &&
                        (F != F3)) {
                        K = F;
                        M = F;
                    }
                    if ((E != D && all_eq4(E, A, G, F, R) &&
                         all_eq2(D, B, H)) &&
                        (D != D3)) {
                        J = D;
                        L = D;
                    }
                    if ((E != H && all_eq4(E, G, I, B, P) &&
                         all_eq2(H, D, F)) &&
                        (H != H3)) {
                        L = H;
                        M = H;
                    }
                    if ((E != B && all_eq4(E, A, C, H, S) &&
                         all_eq2(B, D, F)) &&
                        (B != B3)) {
                        J = B;
                        K = B;
                    }
                    if (Bl < El && all_eq4(E, G, H, I, S) &&
                        none_eq4(E, A, D, C, F)) {
                        J = B;
                        K = B;
                    }
                    if (Hl < El && all_eq4(E, A, B, C, P) &&
                        none_eq4(E, D, G, I, F)) {
                        L = H;
                        M = H;
                    }
                    if (Fl < El && all_eq4(E, A, D, G, Q) &&
                        none_eq4(E, B, C, I, H)) {
                        K = F;
                        M = F;
                    }
                    if (Dl < El && all_eq4(E, C, F, I, R) &&
                        none_eq4(E, B, A, G, H)) {
                        J = D;
                        L = D;
                    }
                }

                // 2:1 slope rules (copy between quadrants; read what earlier
                // rules left in J/K/L/M)
                {
                    if (H != B) {
                        if (H != A && H != E && H != C) {
                            if (all_eq3(H, G, F, R) &&
                                none_eq2(H, D, src_px(src, w, h, x + 2, y - 1)))
                                L = M;
                            if (all_eq3(H, I, D, Q) &&
                                none_eq2(H, F, src_px(src, w, h, x - 2, y - 1)))
                                M = L;
                        }
                        if (B != I && B != G && B != E) {
                            if (all_eq3(B, A, F, R) &&
                                none_eq2(B, D, src_px(src, w, h, x + 2, y + 1)))
                                J = K;
                            if (all_eq3(B, C, D, Q) &&
                                none_eq2(B, F, src_px(src, w, h, x - 2, y + 1)))
                                K = J;
                        }
                    }

                    if (F != D) {
                        if (D != I && D != E && D != C) {
                            if (all_eq3(D, A, H, S) &&
                                none_eq2(D, B, src_px(src, w, h, x + 1, y + 2)))
                                J = L;
                            if (all_eq3(D, G, B, P) &&
                                none_eq2(D, H, src_px(src, w, h, x + 1, y - 2)))
                                L = J;
                        }
                        if (F != E && F != A && F != G) {
                            if (all_eq3(F, C, H, S) &&
                                none_eq2(F, B, src_px(src, w, h, x - 1, y + 2)))
                                K = M;
                            if (all_eq3(F, I, B, P) &&
                                none_eq2(F, H, src_px(src, w, h, x - 1, y - 2)))
                                M = K;
                        }
                    }
                }
            }

            // J top-left, K top-right, L bottom-left, M bottom-right.
            std::uint32_t* row0 = dst + (y * 2) * (w * 2) + x * 2;
            std::uint32_t* row1 = row0 + (w * 2);
            row0[0] = J;
            row0[1] = K;
            row1[0] = L;
            row1[1] = M;
        }
    }
}

// ── the GLSL pass ────────────────────────────────────────────────────────
// Runs as the FIRST present-chain pass: draws the game texture (w×h) into
// a 2w×2h render target. Everything is computed from fragTexCoord (never
// gl_FragCoord): the quad covers the whole target with UV 0..1, so the
// output-pixel index in TEXTURE orientation is floor(uv · 2·size) — immune
// to the render target's Y-storage convention the screen passes deal with
// via uGrid.
//
// uniform uSrc = the SOURCE (logical framebuffer) size in pixels. Taps
// sample at exact texel centres ((floor(p)+0.5)/uSrc), which is exact
// under both POINT and BILINEAR filtering.
//
// Transliteration notes vs the mirror: opaque input assumed (see header);
// luma keeps the reference's +1/255 tie-shift; every extra tap (±3,
// 2:1-rule probes) is fetched unconditionally — identical results (taps
// are pure), no divergent-fetch complexity. The early-out skips only the
// rule evaluation, matching the reference.

inline constexpr const char* kMmpxFragment330 =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec2 uSrc;\n"
    "out vec4 finalColor;\n"
    "vec3 tap(vec2 p) {\n"
    "    return texture(texture0, (floor(p) + 0.5) / uSrc).rgb;\n"
    "}\n"
    "float lum(vec3 c) { return dot(c, vec3(1.0)) + 0.00392157; }\n"
    "void main() {\n"
    "    vec2 opf = floor(fragTexCoord * (uSrc * 2.0));\n"
    "    vec2 spf = floor(opf * 0.5);\n"
    "    spf = min(spf, uSrc - 1.0);\n"
    "    vec2 q = opf - spf * 2.0;\n"
    "    vec3 A = tap(spf + vec2(-1.0, -1.0));\n"
    "    vec3 B = tap(spf + vec2( 0.0, -1.0));\n"
    "    vec3 C = tap(spf + vec2( 1.0, -1.0));\n"
    "    vec3 D = tap(spf + vec2(-1.0,  0.0));\n"
    "    vec3 E = tap(spf + vec2( 0.0,  0.0));\n"
    "    vec3 F = tap(spf + vec2( 1.0,  0.0));\n"
    "    vec3 G = tap(spf + vec2(-1.0,  1.0));\n"
    "    vec3 H = tap(spf + vec2( 0.0,  1.0));\n"
    "    vec3 I = tap(spf + vec2( 1.0,  1.0));\n"
    "    vec3 J = E, K = E, L = E, M = E;\n"
    "    if (A != E || B != E || C != E || D != E || F != E || G != E ||\n"
    "        H != E || I != E) {\n"
    "        vec3 P = tap(spf + vec2(0.0, -2.0));\n"
    "        vec3 S = tap(spf + vec2(0.0,  2.0));\n"
    "        vec3 Q = tap(spf + vec2(-2.0, 0.0));\n"
    "        vec3 R = tap(spf + vec2(2.0,  0.0));\n"
    "        vec3 F3 = tap(spf + vec2(3.0,  0.0));\n"
    "        vec3 D3 = tap(spf + vec2(-3.0, 0.0));\n"
    "        vec3 H3 = tap(spf + vec2(0.0,  3.0));\n"
    "        vec3 B3 = tap(spf + vec2(0.0, -3.0));\n"
    "        vec3 T1 = tap(spf + vec2( 2.0, -1.0));\n"
    "        vec3 T2 = tap(spf + vec2(-2.0, -1.0));\n"
    "        vec3 T3 = tap(spf + vec2( 2.0,  1.0));\n"
    "        vec3 T4 = tap(spf + vec2(-2.0,  1.0));\n"
    "        vec3 T5 = tap(spf + vec2( 1.0,  2.0));\n"
    "        vec3 T6 = tap(spf + vec2( 1.0, -2.0));\n"
    "        vec3 T7 = tap(spf + vec2(-1.0,  2.0));\n"
    "        vec3 T8 = tap(spf + vec2(-1.0, -2.0));\n"
    "        float Bl = lum(B), Dl = lum(D), El = lum(E), Fl = lum(F), Hl = lum(H);\n"
    "        if (D == B && D != H && D != F && (El >= Dl || E == A) &&\n"
    "            (E == A || E == C || E == G) &&\n"
    "            (El < Dl || A != D || E != P || E != Q)) J = D;\n"
    "        if (B == F && B != D && B != H && (El >= Bl || E == C) &&\n"
    "            (E == A || E == C || E == I) &&\n"
    "            (El < Bl || C != B || E != P || E != R)) K = B;\n"
    "        if (H == D && H != F && H != B && (El >= Hl || E == G) &&\n"
    "            (E == A || E == G || E == I) &&\n"
    "            (El < Hl || G != H || E != S || E != Q)) L = H;\n"
    "        if (F == H && F != B && F != D && (El >= Fl || E == I) &&\n"
    "            (E == C || E == G || E == I) &&\n"
    "            (El < Fl || I != H || E != R || E != S)) M = F;\n"
    "        if (E != F && E == C && E == I && E == D && E == Q &&\n"
    "            F == B && F == H && F != F3) { K = F; M = F; }\n"
    "        if (E != D && E == A && E == G && E == F && E == R &&\n"
    "            D == B && D == H && D != D3) { J = D; L = D; }\n"
    "        if (E != H && E == G && E == I && E == B && E == P &&\n"
    "            H == D && H == F && H != H3) { L = H; M = H; }\n"
    "        if (E != B && E == A && E == C && E == H && E == S &&\n"
    "            B == D && B == F && B != B3) { J = B; K = B; }\n"
    "        if (Bl < El && E == G && E == H && E == I && E == S &&\n"
    "            E != A && E != D && E != C && E != F) { J = B; K = B; }\n"
    "        if (Hl < El && E == A && E == B && E == C && E == P &&\n"
    "            E != D && E != G && E != I && E != F) { L = H; M = H; }\n"
    "        if (Fl < El && E == A && E == D && E == G && E == Q &&\n"
    "            E != B && E != C && E != I && E != H) { K = F; M = F; }\n"
    "        if (Dl < El && E == C && E == F && E == I && E == R &&\n"
    "            E != B && E != A && E != G && E != H) { J = D; L = D; }\n"
    "        if (H != B) {\n"
    "            if (H != A && H != E && H != C) {\n"
    "                if (H == G && H == F && H == R && H != D && H != T1) L = M;\n"
    "                if (H == I && H == D && H == Q && H != F && H != T2) M = L;\n"
    "            }\n"
    "            if (B != I && B != G && B != E) {\n"
    "                if (B == A && B == F && B == R && B != D && B != T3) J = K;\n"
    "                if (B == C && B == D && B == Q && B != F && B != T4) K = J;\n"
    "            }\n"
    "        }\n"
    "        if (F != D) {\n"
    "            if (D != I && D != E && D != C) {\n"
    "                if (D == A && D == H && D == S && D != B && D != T5) J = L;\n"
    "                if (D == G && D == B && D == P && D != H && D != T6) L = J;\n"
    "            }\n"
    "            if (F != E && F != A && F != G) {\n"
    "                if (F == C && F == H && F == S && F != B && F != T7) K = M;\n"
    "                if (F == I && F == B && F == P && F != H && F != T8) M = K;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    vec3 outc;\n"
    "    if (q.x < 0.5 && q.y < 0.5) outc = J;\n"
    "    else if (q.x >= 0.5 && q.y < 0.5) outc = K;\n"
    "    else if (q.x < 0.5) outc = L;\n"
    "    else outc = M;\n"
    "    finalColor = vec4(outc, 1.0) * colDiffuse;\n"
    "}\n";

inline constexpr const char* kMmpxFragmentES100 =
    "#version 100\n"
    "precision highp float;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec2 uSrc;\n"
    "vec3 tap(vec2 p) {\n"
    "    return texture2D(texture0, (floor(p) + 0.5) / uSrc).rgb;\n"
    "}\n"
    "float lum(vec3 c) { return dot(c, vec3(1.0)) + 0.00392157; }\n"
    "void main() {\n"
    "    vec2 opf = floor(fragTexCoord * (uSrc * 2.0));\n"
    "    vec2 spf = floor(opf * 0.5);\n"
    "    spf = min(spf, uSrc - 1.0);\n"
    "    vec2 q = opf - spf * 2.0;\n"
    "    vec3 A = tap(spf + vec2(-1.0, -1.0));\n"
    "    vec3 B = tap(spf + vec2( 0.0, -1.0));\n"
    "    vec3 C = tap(spf + vec2( 1.0, -1.0));\n"
    "    vec3 D = tap(spf + vec2(-1.0,  0.0));\n"
    "    vec3 E = tap(spf + vec2( 0.0,  0.0));\n"
    "    vec3 F = tap(spf + vec2( 1.0,  0.0));\n"
    "    vec3 G = tap(spf + vec2(-1.0,  1.0));\n"
    "    vec3 H = tap(spf + vec2( 0.0,  1.0));\n"
    "    vec3 I = tap(spf + vec2( 1.0,  1.0));\n"
    "    vec3 J = E, K = E, L = E, M = E;\n"
    "    if (A != E || B != E || C != E || D != E || F != E || G != E ||\n"
    "        H != E || I != E) {\n"
    "        vec3 P = tap(spf + vec2(0.0, -2.0));\n"
    "        vec3 S = tap(spf + vec2(0.0,  2.0));\n"
    "        vec3 Q = tap(spf + vec2(-2.0, 0.0));\n"
    "        vec3 R = tap(spf + vec2(2.0,  0.0));\n"
    "        vec3 F3 = tap(spf + vec2(3.0,  0.0));\n"
    "        vec3 D3 = tap(spf + vec2(-3.0, 0.0));\n"
    "        vec3 H3 = tap(spf + vec2(0.0,  3.0));\n"
    "        vec3 B3 = tap(spf + vec2(0.0, -3.0));\n"
    "        vec3 T1 = tap(spf + vec2( 2.0, -1.0));\n"
    "        vec3 T2 = tap(spf + vec2(-2.0, -1.0));\n"
    "        vec3 T3 = tap(spf + vec2( 2.0,  1.0));\n"
    "        vec3 T4 = tap(spf + vec2(-2.0,  1.0));\n"
    "        vec3 T5 = tap(spf + vec2( 1.0,  2.0));\n"
    "        vec3 T6 = tap(spf + vec2( 1.0, -2.0));\n"
    "        vec3 T7 = tap(spf + vec2(-1.0,  2.0));\n"
    "        vec3 T8 = tap(spf + vec2(-1.0, -2.0));\n"
    "        float Bl = lum(B), Dl = lum(D), El = lum(E), Fl = lum(F), Hl = lum(H);\n"
    "        if (D == B && D != H && D != F && (El >= Dl || E == A) &&\n"
    "            (E == A || E == C || E == G) &&\n"
    "            (El < Dl || A != D || E != P || E != Q)) J = D;\n"
    "        if (B == F && B != D && B != H && (El >= Bl || E == C) &&\n"
    "            (E == A || E == C || E == I) &&\n"
    "            (El < Bl || C != B || E != P || E != R)) K = B;\n"
    "        if (H == D && H != F && H != B && (El >= Hl || E == G) &&\n"
    "            (E == A || E == G || E == I) &&\n"
    "            (El < Hl || G != H || E != S || E != Q)) L = H;\n"
    "        if (F == H && F != B && F != D && (El >= Fl || E == I) &&\n"
    "            (E == C || E == G || E == I) &&\n"
    "            (El < Fl || I != H || E != R || E != S)) M = F;\n"
    "        if (E != F && E == C && E == I && E == D && E == Q &&\n"
    "            F == B && F == H && F != F3) { K = F; M = F; }\n"
    "        if (E != D && E == A && E == G && E == F && E == R &&\n"
    "            D == B && D == H && D != D3) { J = D; L = D; }\n"
    "        if (E != H && E == G && E == I && E == B && E == P &&\n"
    "            H == D && H == F && H != H3) { L = H; M = H; }\n"
    "        if (E != B && E == A && E == C && E == H && E == S &&\n"
    "            B == D && B == F && B != B3) { J = B; K = B; }\n"
    "        if (Bl < El && E == G && E == H && E == I && E == S &&\n"
    "            E != A && E != D && E != C && E != F) { J = B; K = B; }\n"
    "        if (Hl < El && E == A && E == B && E == C && E == P &&\n"
    "            E != D && E != G && E != I && E != F) { L = H; M = H; }\n"
    "        if (Fl < El && E == A && E == D && E == G && E == Q &&\n"
    "            E != B && E != C && E != I && E != H) { K = F; M = F; }\n"
    "        if (Dl < El && E == C && E == F && E == I && E == R &&\n"
    "            E != B && E != A && E != G && E != H) { J = D; L = D; }\n"
    "        if (H != B) {\n"
    "            if (H != A && H != E && H != C) {\n"
    "                if (H == G && H == F && H == R && H != D && H != T1) L = M;\n"
    "                if (H == I && H == D && H == Q && H != F && H != T2) M = L;\n"
    "            }\n"
    "            if (B != I && B != G && B != E) {\n"
    "                if (B == A && B == F && B == R && B != D && B != T3) J = K;\n"
    "                if (B == C && B == D && B == Q && B != F && B != T4) K = J;\n"
    "            }\n"
    "        }\n"
    "        if (F != D) {\n"
    "            if (D != I && D != E && D != C) {\n"
    "                if (D == A && D == H && D == S && D != B && D != T5) J = L;\n"
    "                if (D == G && D == B && D == P && D != H && D != T6) L = J;\n"
    "            }\n"
    "            if (F != E && F != A && F != G) {\n"
    "                if (F == C && F == H && F == S && F != B && F != T7) K = M;\n"
    "                if (F == I && F == B && F == P && F != H && F != T8) M = K;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    vec3 outc;\n"
    "    if (q.x < 0.5 && q.y < 0.5) outc = J;\n"
    "    else if (q.x >= 0.5 && q.y < 0.5) outc = K;\n"
    "    else if (q.x < 0.5) outc = L;\n"
    "    else outc = M;\n"
    "    gl_FragColor = vec4(outc, 1.0) * colDiffuse;\n"
    "}\n";

inline const char* fragment_source() {
#if defined(__ANDROID__)
    return kMmpxFragmentES100;
#else
    return kMmpxFragment330;
#endif
}

}  // namespace gbarecomp::mmpx
