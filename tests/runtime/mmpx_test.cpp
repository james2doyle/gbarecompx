// mmpx_test.cpp — the MMPX edge-smoothing pass, red-first.
//
// Three layers of coverage:
//  1. Differential: our CPU mirror (src/runtime/mmpx.h scale2x) must match
//     the VENDORED MIT reference (support/mmpx_reference_c99.c — the
//     authors' C99, test-only) byte-for-byte on random and structured
//     inputs. This is the transcription-bug net: any rule mistranscribed
//     diverges somewhere in the random corpus.
//  2. Pattern pins (hand-derived from the rule cascade): the invariants
//     FFTA cares about — flat passthrough, straight edges stay sharp (the
//     EPX-rounding regression class), perfect 45° staircases pass through
//     (1px features are content, not noise), and palette exactness (every
//     output value is a source value).
//  3. GLSL structure pins: the shader bodies carry the distinctive rule
//     text and the centre-tap formula, so the transliteration cannot
//     silently drift from the mirror (the same property the LCD mask-
//     multiply pin keeps). Both dialects must be dialect-complete.
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG.
#include "mmpx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The vendored reference (global-namespace mmpx_scale2x, MIT).
extern "C" void mmpx_scale2x(const unsigned int* srcBuffer,
                             unsigned int* dst, int srcWidth, int srcHeight);

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

// Deterministic LCG (no <random>: the corpus must be identical everywhere).
std::uint32_t lcg_state = 0x12345678u;
std::uint32_t lcg() {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state >> 8;
}

// Packed RGBA from three 8-bit channels (opaque).
constexpr std::uint32_t rgb(std::uint32_t r, std::uint32_t g,
                            std::uint32_t b) {
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

constexpr int kMaxDim = 48;

bool run_both(const std::uint32_t* src, int w, int h,
              std::uint32_t* out_mine, std::uint32_t* out_ref) {
    std::memset(out_mine, 0xAA,
                sizeof(std::uint32_t) * 2 * w * 2 * h);
    std::memset(out_ref, 0xBB, sizeof(std::uint32_t) * 2 * w * 2 * h);
    gbarecomp::mmpx::scale2x(src, out_mine, w, h);
    mmpx_scale2x(src, out_ref, w, h);
    return std::memcmp(out_mine, out_ref,
                       sizeof(std::uint32_t) * 2 * w * 2 * h) == 0;
}

}  // namespace

int main() {
    using namespace gbarecomp::mmpx;

    // ── 1. differential vs the vendored reference ───────────────────────
    {
        static std::uint32_t src[kMaxDim * kMaxDim];
        static std::uint32_t mine[2 * kMaxDim * 2 * kMaxDim];
        static std::uint32_t ref[2 * kMaxDim * 2 * kMaxDim];

        // Hand-built structured patterns: flat, vertical edge, horizontal
        // edge, perfect diagonal, checker, 1px lines both orientations.
        struct Pattern {
            const char* name;
            int w, h;
            std::uint32_t px[8 * 8];
        };
        const std::uint32_t X = rgb(40, 60, 90), Y = rgb(200, 200, 180);
        Pattern patterns[] = {
            {"flat 3x3", 3, 3, {X, X, X, X, X, X, X, X, X}},
            {"vertical edge 4x3",
             4, 3, {X, X, Y, Y, X, X, Y, Y, X, X, Y, Y}},
            {"horizontal edge 3x4",
             3, 4, {X, X, X, X, X, X, Y, Y, Y, Y, Y, Y}},
            {"diagonal 4x4",
             4, 4, {X, Y, Y, Y, Y, X, Y, Y, Y, Y, X, Y, Y, Y, Y, X}},
            {"checker 4x4",
             4, 4, {X, Y, X, Y, Y, X, Y, X, X, Y, X, Y, Y, X, Y, X}},
            {"1px vline 5x3",
             5, 3, {Y, X, Y, Y, Y, Y, X, Y, Y, Y, Y, X, Y, Y, Y}},
            {"1px hline 3x5",
             3, 5, {Y, Y, Y, X, X, X, Y, Y, Y, X, X, X, Y, Y, Y}},
            {"corner L 4x4",
             4, 4, {X, X, X, X, X, Y, Y, Y, X, Y, Y, Y, X, Y, Y, Y}},
        };
        bool all_structured = true;
        for (const auto& p : patterns) {
            if (!run_both(p.px, p.w, p.h, mine, ref)) {
                all_structured = false;
                std::printf("        (pattern %s diverged)\n", p.name);
            }
        }
        check(all_structured, "structured patterns match the reference");

        // Random corpus: small palettes (rules fire constantly) and full
        // random; sizes from degenerate-tiny to mid.
        bool all_random = true;
        int diverged_at = -1;
        for (int trial = 0; trial < 160; ++trial) {
            const int w = 4 + static_cast<int>(lcg() % 32);
            const int h = 4 + static_cast<int>(lcg() % 32);
            const int palette_n =
                trial % 2 == 0 ? 4 : 16;  // tiny palette vs rich
            std::uint32_t palette[16];
            for (int i = 0; i < palette_n; ++i)
                palette[i] = rgb(lcg() & 0xFF, lcg() & 0xFF, lcg() & 0xFF);
            for (int i = 0; i < w * h; ++i)
                src[i] = palette[lcg() % static_cast<std::uint32_t>(palette_n)];
            if (!run_both(src, w, h, mine, ref)) {
                all_random = false;
                diverged_at = trial;
                break;
            }
        }
        check(all_random, "random corpus (160 images) matches the reference");
        if (!all_random) std::printf("        (first divergence: trial %d)\n", diverged_at);
    }

    // ── 2. pattern pins (the FFTA-relevant invariants) ──────────────────
    {
        // Flat region: the early-out — every output pixel = the source.
        {
            const std::uint32_t X = rgb(10, 20, 30);
            const std::uint32_t src[9] = {X, X, X, X, X, X, X, X, X};
            std::uint32_t dst[36];
            scale2x(src, dst, 3, 3);
            bool uniform = true;
            for (int i = 0; i < 36; ++i) uniform = uniform && dst[i] == X;
            check(uniform, "flat region passes through untouched");
        }
        // Straight vertical edge: BOTH sides of the boundary are uniform
        // blocks — no EPX-style rounding notch. 4x2, left X right Y.
        {
            const std::uint32_t X = rgb(40, 60, 90), Y = rgb(200, 200, 180);
            const std::uint32_t src[8] = {X, X, Y, Y, X, X, Y, Y};
            std::uint32_t dst[32];
            scale2x(src, dst, 4, 2);
            // Left of the boundary (source col 1 -> dst cols 2,3): all X.
            bool left_ok = dst[2] == X && dst[3] == X &&
                           dst[2 + 8] == X && dst[3 + 8] == X;
            // Right of the boundary (source col 2 -> dst cols 4,5): all Y.
            bool right_ok = dst[4] == Y && dst[5] == Y &&
                            dst[4 + 8] == Y && dst[5 + 8] == Y;
            check(left_ok && right_ok,
                  "straight vertical edges stay sharp (no rounding notch)");
        }
        // Perfect 45° staircase: INTERIOR diagonal pixels (both diagonal
        // neighbors present) keep uniform 2x2 blocks — the 1px line stays
        // 1 game-px wide. (Line END blocks legitimately get their trailing
        // corner rounded — rule 4/2 reconstructs line ends; that is the
        // algorithm's documented corner behavior, verified matching the
        // reference by the differential test.)
        {
            const std::uint32_t X = rgb(40, 60, 90), Y = rgb(200, 200, 180);
            // 8x8 with a diagonal from (1,1) to (6,6).
            std::uint32_t src[64];
            for (int i = 0; i < 64; ++i) src[i] = Y;
            for (int k = 1; k <= 6; ++k) src[k * 8 + k] = X;
            std::uint32_t dst[256];
            scale2x(src, dst, 8, 8);
            bool interior_uniform = true;
            for (int k = 2; k <= 5 && interior_uniform; ++k) {
                const std::uint32_t* b =
                    dst + (k * 2) * 16 + k * 2;
                interior_uniform = b[0] == X && b[1] == X && b[16] == X &&
                                   b[17] == X;
            }
            check(interior_uniform,
                  "staircase interior passes through (1px features kept)");
        }
        // Palette exactness on a rules-heavy pattern: every output value is
        // SOME source value (no blends — the MMPX contract).
        {
            const std::uint32_t X = rgb(40, 60, 90), Y = rgb(200, 200, 180),
                                Z = rgb(90, 140, 60);
            const std::uint32_t src[16] = {X, Y, Z, Y, Y, X, Y, Z,
                                           Z, Y, X, Y, Y, Z, Y, X};
            std::uint32_t dst[64];
            scale2x(src, dst, 4, 4);
            bool exact = true;
            for (int i = 0; i < 64; ++i)
                exact = exact &&
                        (dst[i] == X || dst[i] == Y || dst[i] == Z);
            check(exact, "every output value is a source value (palette exact)");
        }
        // Degenerate sizes are no-ops, not crashes.
        {
            std::uint32_t dst[4] = {1, 2, 3, 4};
            scale2x(nullptr, dst, 0, 0);
            check(dst[0] == 1 && dst[3] == 4, "degenerate input is a no-op");
        }
    }

    // ── 3. GLSL structure pins ──────────────────────────────────────────
    {
        // The centre-tap formula: taps sample texel CENTRES (the +0.5/uSrc
        // convention) — the property that makes the pass filter-agnostic
        // and border-clamped.
        check(std::strstr(kMmpxFragment330, "(floor(p) + 0.5) / uSrc") != nullptr &&
                  std::strstr(kMmpxFragmentES100, "(floor(p) + 0.5) / uSrc") != nullptr,
              "both dialects tap texel centres");
        // The output index comes from fragTexCoord (never gl_FragCoord —
        // the RT Y-convention must not leak into this pass).
        check(std::strstr(kMmpxFragment330, "fragTexCoord * (uSrc * 2.0)") != nullptr &&
                  std::strstr(kMmpxFragment330, "gl_FragCoord") == nullptr,
              "330 derives geometry from fragTexCoord only");
        check(std::strstr(kMmpxFragmentES100, "fragTexCoord * (uSrc * 2.0)") != nullptr &&
                  std::strstr(kMmpxFragmentES100, "gl_FragCoord") == nullptr,
              "ES100 derives geometry from fragTexCoord only");
        // Distinctive rule text: a 1:1 slope rule, an intersection rule,
        // and a 2:1 copy — one from each group, both dialects.
        check(std::strstr(kMmpxFragment330, "(El < Dl || A != D || E != P || E != Q)) J = D;") != nullptr,
              "330 carries the 1:1 slope rule verbatim");
        check(std::strstr(kMmpxFragment330, "F == B && F == H && F != F3) { K = F; M = F; }") != nullptr,
              "330 carries the intersection rule verbatim");
        check(std::strstr(kMmpxFragment330, "H != T1) L = M;") != nullptr,
              "330 carries the 2:1 copy verbatim");
        check(std::strstr(kMmpxFragmentES100, "(El < Dl || A != D || E != P || E != Q)) J = D;") != nullptr,
              "ES100 carries the 1:1 slope rule verbatim");
        check(std::strstr(kMmpxFragmentES100, "H != T1) L = M;") != nullptr,
              "ES100 carries the 2:1 copy verbatim");
        // The early-out (uniform 3x3 -> nearest) exists in both.
        check(std::strstr(kMmpxFragment330, "A != E || B != E || C != E || D != E") != nullptr &&
                  std::strstr(kMmpxFragmentES100, "A != E || B != E || C != E || D != E") != nullptr,
              "both dialects carry the uniform-region early-out");
        // Android dialect completeness: highp declared (the LADXHD
        // ShaderPatch lesson — precision discipline for rule math).
        check(std::strstr(kMmpxFragmentES100, "precision highp float;") != nullptr,
              "ES100 declares highp");
    }

    if (failures == 0) std::printf("mmpx tests: PASS\n");
    else std::printf("mmpx tests: FAIL (%d failure(s))\n", failures);
    return failures == 0 ? 0 : 1;
}
