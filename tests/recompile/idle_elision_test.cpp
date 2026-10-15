// Stage-2 idle-loop elision nomination: a quiescent spin whose body holds
// interior FORWARD branches (flag-set exit path) must still nominate its
// back-edge for the runtime_idle_backedge() probe; bodies with calls or
// backward interior branches must not. Motivating case: FFTA's
// vblank_wait_loop (0x08000418-0x08000428) — without nomination it executes
// ~200k native iterations/frame (~17ms on mobile, the raylib-Android intro
// hitch, 2026-10-14). Straight-line spins keep their long-standing probe.
#include "emit_function.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr std::uint32_t kBase = 0x08000000u;
int failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL idle_elision: %s\n", what);
    ++failures;
}

struct Rom {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x1000, 0);
    void h(std::uint32_t pc, std::uint16_t v) {
        bytes[pc - kBase] = static_cast<std::uint8_t>(v);
        bytes[pc - kBase + 1] = static_cast<std::uint8_t>(v >> 8);
    }
    void w(std::uint32_t pc, std::uint32_t v) {
        h(pc, static_cast<std::uint16_t>(v));
        h(pc + 2, static_cast<std::uint16_t>(v >> 16));
    }
    // Thumb B (conditional: 8-bit offset; unconditional: 11-bit) to target.
    void b(std::uint32_t pc, std::uint32_t target, std::uint16_t base) {
        const std::int32_t off = static_cast<std::int32_t>(target - (pc + 4u));
        const std::uint32_t mask = (base & 0xFF00u) == 0xD000u ? 0xFFu : 0x7FFu;
        h(pc, static_cast<std::uint16_t>(base | ((off >> 1) & mask)));
    }
    void thumb_bl(std::uint32_t pc, std::uint32_t target) {
        const std::int32_t off = static_cast<std::int32_t>(target - (pc + 4u));
        h(pc, static_cast<std::uint16_t>(0xF000u | ((off >> 12) & 0x7FF)));
        h(pc + 2, static_cast<std::uint16_t>(0xF800u | ((off >> 1) & 0x7FF)));
    }
};

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

std::string emit_at(Rom& rom, std::uint32_t head, std::uint32_t end) {
    gbarecomp::Function fn{};
    fn.addr = head;
    fn.source_addr = head;
    fn.mode = gbarecomp::CpuMode::Thumb;
    fn.name = "t";
    fn.end_addr = end;
    const std::unordered_map<std::uint64_t, std::string> names;
    return gbarecomp::emit_function_body_str(fn, rom.bytes.data(),
                                             rom.bytes.size(), kBase, names);
}

}  // namespace

int main() {
    Rom rom;

    // Branchy vblank-spin shape (FFTA 0x08000418, relocated here):
    //   H+0x0: ldr  r0,[pc,#8]   (flag address)
    //   H+0x2: ldrh r1,[r0]       (flag value)
    //   H+0x4: cmp  r1,#0
    //   H+0x6: beq  H+0x10        (still waiting: keep spinning)
    //   H+0x8: b    H+0x12        (flag set: exit to work)
    //   H+0xC: pool word (data, decodes idle-simple)
    //   H+0x10: b   H             (back-edge)
    const std::uint32_t spin = 0x08000100u;
    rom.h(spin + 0x0, 0x4802u);          // ldr r0,[pc,#8]
    rom.h(spin + 0x2, 0x8801u);          // ldrh r1,[r0]
    rom.h(spin + 0x4, 0x2900u);          // cmp r1,#0
    rom.b(spin + 0x6, spin + 0x10, 0xD000u);  // beq (forward, in-body)
    rom.b(spin + 0x8, spin + 0x12, 0xE000u);  // b (forward exit)
    rom.h(spin + 0xA, 0x0000u);          // filler (lsls, idle-simple)
    rom.w(spin + 0xC, 0x03000E10u);      // literal pool (data)
    rom.b(spin + 0x10, spin, 0xE000u);   // back-edge
    const std::string spin_body = emit_at(rom, spin, spin + 0x12u);
    check(contains(spin_body, "runtime_idle_backedge(0x08000100u)"),
          "branchy spin back-edge missing its idle probe");

    // Call in the body: the BL is a real side effect — must NOT nominate.
    const std::uint32_t callspin = 0x08000200u;
    rom.h(callspin + 0x0, 0x4802u);      // ldr r0,[pc,#8]
    rom.thumb_bl(callspin + 0x2, 0x08000500u);  // bl work (4 bytes)
    rom.b(callspin + 0x6, callspin, 0xE000u);   // back-edge
    rom.w(callspin + 0x8, 0x03000E10u);  // pool
    const std::string call_body = emit_at(rom, callspin, callspin + 0xCu);
    check(!contains(call_body, "runtime_idle_backedge"),
          "call in spin body must not nominate an idle probe");

    // Straight-line spin (pre-existing behavior): ldr/cmp/back-edge.
    const std::uint32_t plain = 0x08000300u;
    rom.h(plain + 0x0, 0x4802u);         // ldr r0,[pc,#8]
    rom.h(plain + 0x2, 0x8801u);         // ldrh r1,[r0]
    rom.h(plain + 0x4, 0x2900u);         // cmp r1,#0
    rom.b(plain + 0x6, plain, 0xE000u);  // back-edge
    rom.w(plain + 0x8, 0x03000E10u);     // pool
    const std::string plain_body = emit_at(rom, plain, plain + 0xCu);
    check(contains(plain_body, "runtime_idle_backedge(0x08000300u)"),
          "straight-line spin lost its idle probe");

    if (failures) return 1;
    std::printf("idle_elision_tests: OK\n");
    return 0;
}
