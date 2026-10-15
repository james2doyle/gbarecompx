// Spike: drive the real RaylibBackend on an Android device through the same
// calls the desktop integration test makes (open/present/pump/close), and
// report over logcat. Audio disabled: rung 2 is render+input, audio is rung 3.
#include <android/log.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sys/stat.h>
#include <vector>

#include "raylib.h"
#include "raylib_backend.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "RAYTEST", __VA_ARGS__)

namespace {

int s_pass = 0, s_fail = 0;

void check(bool ok, const char* name) {
    if (ok) {
        ++s_pass;
        LOGI("ok: %s", name);
    } else {
        ++s_fail;
        LOGI("FAIL: %s", name);
    }
}

constexpr int kW = 240, kH = 160;

std::vector<std::uint8_t> make_frame(int tag) {
    std::vector<std::uint8_t> f(static_cast<size_t>(kW) * kH * 3);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const size_t i = (static_cast<size_t>(y) * kW + x) * 3;
            f[i + 0] = static_cast<std::uint8_t>((x * 255) / kW);
            f[i + 1] = static_cast<std::uint8_t>((y * 255) / kH);
            f[i + 2] = static_cast<std::uint8_t>(tag & 0xFF);
        }
    return f;
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    LOGI("starting (audio rung: sine push at emu cadence)");
    gbarecomp::RaylibBackend b;
    check(!b.is_open(), "backend starts closed");

    // Audio left enabled (default): open() must bring up the real device,
    // not the graceful-degradation path.
    const bool opened = b.open(/*scale=*/2, kW, kH, "raylib android",
                               /*screen=*/"raw", /*linear_filter=*/false,
                               /*sharp_filter=*/false,
                               /*resize_driven_view=*/false,
                               /*freely_resizable_window=*/true);
    check(opened, "open() succeeded");
    check(b.is_open(), "is_open() after open");
    if (!opened) {
        LOGI("DONE pass=%d fail=%d (open failed; game over)", s_pass, s_fail);
        return 1;
    }
    check(IsAudioDeviceReady() != 0, "audio device ready after open");

    int dw = 0, dh = 0;
    b.drawable_size(&dw, &dh);
    LOGI("drawable=%dx%d", dw, dh);
    check(dw > 0 && dh > 0, "drawable nonzero");

    // Present path + CPU readback mirror.
    const std::vector<std::uint8_t> src = make_frame(151);
    b.present(src.data(), /*fresh=*/true);
    std::vector<std::uint8_t> got;
    int ew = 0, eh = 0;
    check(b.copy_presented_frame(got, ew, eh) && got == src,
          "present keeps the CPU frame exact");
    check(ew == kW && eh == kH, "readback dims match");

    // Shaders: no GLSL-ES variants yet — the request is recorded (same intent
    // semantics as desktop: enabled() reports the request) but nothing
    // engages: present gates on flag&&ready and the loaders decline. The
    // observable proof is the exact frame below, not the getter.
    b.set_lcd_shader(true);
    check(b.lcd_shader_enabled(), "lcd request recorded like desktop");
    b.set_lcd_dither(1.0f);
    b.set_lcd_deband(6.0f);
    b.present(src.data(), /*fresh=*/true);
    check(b.copy_presented_frame(got, ew, eh) && got == src,
          "frame exact with declined shaders requested");

    // Pump baseline: nothing touched — expect the all-released word, no quit.
    const auto ev = b.pump();
    LOGI("pump: keyinput=0x%04X quit=%d slots=%d/%d", ev.keyinput, (int)ev.quit,
         ev.save_slot, ev.load_slot);
    check(ev.keyinput == 0x03FFu, "pump baseline keyinput all-released");
    check(!ev.quit, "pump baseline raises no quit");
    const auto ev2 = b.pump();
    check(ev2.keyinput == ev.keyinput && !ev2.quit,
          "second idle pump re-fires nothing");

    LOGI("gamepad_connected=%d", (int)b.gamepad_connected());

    // A few more frames for thermal/stability signal, then report and idle
    // (the harness force-stops us after reading DONE).
    for (int i = 0; i < 30; ++i) b.present(src.data(), /*fresh=*/false);
    LOGI("30 repeat presents done");

    // ---- audio rung: 440 Hz sine at emu cadence (~1092 S16 samples/frame
    // at 65536 Hz source rate), 600 frames ≈ 10 s of sustained push→drain.
    // Proves the OpenSL ES device, float stream, RAB resample and callback
    // survive real-time flow on device. Underrun tuning stays desktop until
    // a human listens; this rung is init + stability + clean teardown.
    {
        constexpr double kSrcRate = 65536.0;
        constexpr double kFreq = 440.0;
        constexpr int kPerFrame = 1092;
        double phase = 0.0;
        const double step = 2.0 * 3.141592653589793 * kFreq / kSrcRate;
        std::vector<std::int16_t> sine(kPerFrame);
        for (int f = 0; f < 600; ++f) {
            for (int i = 0; i < kPerFrame; ++i) {
                sine[i] = static_cast<std::int16_t>(
                    12000.0 * std::sin(phase));
                phase += step;
            }
            b.push_audio_samples(sine.data(), sine.size());
            b.present(src.data(), /*fresh=*/false);
            b.pump();
            if ((f % 120) == 119) LOGI("audio flowing: %d/600 frames", f + 1);
        }
        check(b.is_open(), "alive after 10 s of audio flow");
    }

    // NOTE: close→reopen in one activity is unsupported by raylib Android
    // itself (second InitWindow waits on an INIT_WINDOW the OS never
    // re-sends). Proven 2026-10-14; the port must pause/rebind instead.
    // So the screenshots below run on the still-open backend, before close.
    // Capture-lag calibration: distinct card tag per combo identifies which
    // combo's pixels each screenshot actually holds (takes the guesswork out
    // of swap/backbuffer semantics). Two captures per combo expose variance.
    {
        auto shot2 = [&](const char* a, const char* bname, int tag) {
            const std::vector<std::uint8_t> card = make_frame(tag);
            for (int i = 0; i < 4; ++i) b.present(card.data(), true);
            TakeScreenshot(a);
            b.present(card.data(), false);
            TakeScreenshot(bname);
            LOGI("shots: %s %s (tag %d)", a, bname, tag);
        };
        b.set_lcd_shader(false);
        b.set_lcd_dither(0.0f);
        b.set_lcd_deband(0.0f);
        shot2("tag_off_a.png", "tag_off_b.png", 10);
        b.set_lcd_shader(true);
        shot2("tag_lcd_a.png", "tag_lcd_b.png", 20);
        b.set_lcd_dither(1.0f);
        b.set_lcd_deband(6.0f);
        shot2("tag_all_a.png", "tag_all_b.png", 30);
        std::vector<std::uint8_t> tail;
        int tw = 0, th = 0;
        check(b.copy_presented_frame(tail, tw, th) && !tail.empty(),
              "readback serves frames");
    }
    b.close();
    check(!b.is_open(), "close() releases");
    LOGI("DONE pass=%d fail=%d", s_pass, s_fail);
    for (;;) usleep(1000000);
    return 0;
}
