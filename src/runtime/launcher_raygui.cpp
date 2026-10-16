// launcher_raygui.cpp — raygui LauncherBackend (framework).
//
// Concrete backend behind the gbarecomp::LauncherBackend contract. Renders a
// minimal pre-boot screen (box art, Play, Select ROM, drag-and-drop, gamepad
// nav) and drives a LaunchScreenModel for state. main() sees only the
// contract types — no raygui/raylib headers leak past this TU.
//
// RAYGUI_IMPLEMENTATION lives in raygui_impl.cpp (the raygui menu engine
// shares that single implementation TU); this TU includes raygui.h
// declarations-only. Compiled into gbarecomp_runtime only when raylib is
// enabled.

#include "launcher.h"

#if defined(GBARECOMP_USE_RAYLIB)

#include "raygui/raygui.h"
#include <raylib.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "launch_screen_model.h"
#include "asset_picker.h"
#include "launcher_style.h"

namespace gbarecomp {
namespace {

constexpr int kWindowW = 720;
constexpr int kWindowH = 480;
constexpr int kBtnW = 200;
constexpr int kBtnH = 48;

// launcher_style.h values are raylib-free uint32 RGB; this TU renders them.
Color rgb(std::uint32_t v) {
    return Color{static_cast<unsigned char>((v >> 16) & 0xFF),
                 static_cast<unsigned char>((v >> 8) & 0xFF),
                 static_cast<unsigned char>(v & 0xFF), 0xFF};
}

// The measurer adapter for launcher_style::wrap_words.
float measure_raylib(const char* text, float size) {
    return MeasureText(text, static_cast<int>(size));
}

enum Focus { kFocusPlay = 0, kFocusSelect = 1, kFocusCount = 2 };

struct Layout {
    Rectangle play{0, 0, static_cast<float>(kBtnW), static_cast<float>(kBtnH)};
    Rectangle select{0, 0, static_cast<float>(kBtnW), static_cast<float>(kBtnH)};
    Rectangle boxart{0, 0, 320, 320};
};

Layout compute_layout(int art_w, int art_h) {
    Layout l;
    (void)art_h;
    const float aspect = (art_h > 0) ? (float)art_w / (float)art_h : (456.0f / 528.0f);
    const float box_w = 320.0f;
    const float box_h = box_w / aspect;
    // Left (even) column: box art preserving aspect ratio, centered.
    const float box_left = static_cast<float>((kWindowW / 2) - (int)box_w) / 2.0f;
    const float box_top  = (static_cast<float>(kWindowH) - box_h) / 2.0f;
    l.boxart = Rectangle{box_left, box_top, box_w, box_h};
    // Right column: Play then Select ROM, stacked.
    const float right_x = static_cast<float>(kWindowW / 2) + 20.0f;
    l.play = Rectangle{right_x, 150.0f, 220.0f, static_cast<float>(kBtnH)};
    l.select = Rectangle{right_x, 212.0f, 220.0f, static_cast<float>(kBtnH)};
    return l;
}

// Permissive ROM spec for the launch screen: any existing readable file
// validates (the strict size/SHA-1/CRC32 gate is run_game's job, not the
// launch screen's). The pick is cached so the next launch skips the screen.
AssetSpec make_rom_spec() {
    AssetSpec spec;
    spec.display_name = "GBA ROM";
    spec.dialog_filter = "GBA ROM (*.gba)\0*.gba\0";
    spec.dialog_title = "Select ROM";
    spec.cache_filename = "rom.cfg";
    spec.expected_size = 0;
    spec.expected_sha1 = nullptr;
    spec.expected_crc32 = 0;
    spec.persist_cache = true;
    return spec;
}

class RayguiBackend : public LauncherBackend {
public:
    LauncherResult run(const LauncherConfig& cfg) override;
};

LauncherResult RayguiBackend::run(const LauncherConfig& cfg) {
    const std::string argv0 = cfg.argv0 ? cfg.argv0 : ".";
    const std::string staged_bios = cfg.staged_bios ? cfg.staged_bios : "";

    LaunchScreenModel model(make_rom_spec(), argv0, staged_bios);

    InitWindow(kWindowW, kWindowH, launcher_style::kGameTitle);
    SetTargetFPS(60);

    Texture2D boxart{};
    bool have_boxart = false;
    std::string resolved_boxart;
    if (cfg.boxart_path && *cfg.boxart_path) {
        resolved_boxart = cfg.boxart_path;
    } else {
        resolved_boxart = std::string(GetApplicationDirectory()) + "assets/img/boxart.png";
    }
    if (!resolved_boxart.empty()) {
        Image img = LoadImage(resolved_boxart.c_str());
        if (!img.data) {
            // Raylib may be built without TGA support; fall back to a sidecar PNG.
            if (resolved_boxart.rfind(".tga") == resolved_boxart.size() - 4) {
                img = LoadImage((resolved_boxart.substr(0, resolved_boxart.size() - 4) + ".png").c_str());
            }
        }
        if (img.data) {
            boxart = LoadTextureFromImage(img);
            UnloadImage(img);
            have_boxart = true;
        }
    }

    const int art_w = have_boxart ? boxart.width : 456;
    const int art_h = have_boxart ? boxart.height : 528;
    const Layout layout = compute_layout(art_w, art_h);
    Focus focus = kFocusPlay;
    bool first_boot_autoopened = false;
    std::string notice;
    LauncherResult pending;
    bool have_pending = false;

    while (!WindowShouldClose() && !have_pending) {
        // First-boot auto-open: NeedRom + first_boot_pick -> open the picker.
        if (cfg.first_boot_pick &&
            model.state() == LaunchScreenState::NeedRom &&
            !first_boot_autoopened) {
            first_boot_autoopened = true;
            auto r = resolve_asset("", make_rom_spec(), argv0);
            if (r.ok) model.apply(LaunchScreenInput::RomPicked, r.path);
            else notice = r.error;
            SetWindowFocused();
        }

        // ── input → model ────────────────────────────────────────────────
        // Drag-and-drop: .gba file -> RomDropped.
        if (IsFileDropped()) {
            FilePathList dropped = LoadDroppedFiles();
            for (unsigned int i = 0; i < dropped.count; ++i) {
                const char* path = dropped.paths[i];
                const char* ext = strrchr(path, '.');
                if (ext && strcasecmp(ext, ".gba") == 0) {
                    model.apply(LaunchScreenInput::RomDropped, path);
                    break;
                }
            }
            UnloadDroppedFiles(dropped);
        }

        // Keyboard: Left/Right/Tab move focus, Enter/Space activates,
        // Escape quits, R opens the ROM picker.
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) ||
            IsKeyPressed(KEY_TAB)) {
            focus = (focus == kFocusPlay) ? kFocusSelect : kFocusPlay;
        }
        if (IsKeyPressed(KEY_R)) {
            auto r = resolve_asset("", make_rom_spec(), argv0, true);
            if (r.ok) model.apply(LaunchScreenInput::RomPicked, r.path);
            else notice = r.error;
            SetWindowFocused();
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            auto out = model.apply(LaunchScreenInput::QuitPressed);
            if (out == LaunchScreenOutput::Quit) {
                pending.kind = LauncherResult::Kind::Quit;
                have_pending = true;
            }
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
            if (focus == kFocusPlay) {
                auto out = model.apply(LaunchScreenInput::PlayPressed);
                if (out == LaunchScreenOutput::Launch) {
                    pending.kind = LauncherResult::Kind::Launch;
                    pending.rom = model.rom_path();
                    pending.bios = staged_bios;
                    have_pending = true;
                }
            } else {
                auto r = resolve_asset("", make_rom_spec(), argv0, true);
                if (r.ok) model.apply(LaunchScreenInput::RomPicked, r.path);
                else notice = r.error;
            }
        }

        // Gamepad: d-pad left/right moves focus, A/Cross activates,
        // B/Circle quits, X/Square opens the ROM picker.
        if (IsGamepadAvailable(0)) {
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) {
                focus = (focus == kFocusPlay) ? kFocusSelect : kFocusPlay;
            }
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
                if (focus == kFocusPlay) {
                    auto out = model.apply(LaunchScreenInput::PlayPressed);
                    if (out == LaunchScreenOutput::Launch) {
                        pending.kind = LauncherResult::Kind::Launch;
                        pending.rom = model.rom_path();
                        pending.bios = staged_bios;
                        have_pending = true;
                    }
                } else {
                    auto r = resolve_asset("", make_rom_spec(), argv0, true);
                    if (r.ok) model.apply(LaunchScreenInput::RomPicked, r.path);
                }
            }
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) {
                auto r = resolve_asset("", make_rom_spec(), argv0, true);
                if (r.ok) model.apply(LaunchScreenInput::RomPicked, r.path);
                else notice = r.error;
            }
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP)) {
                auto out = model.apply(LaunchScreenInput::QuitPressed);
                if (out == LaunchScreenOutput::Quit) {
                    pending.kind = LauncherResult::Kind::Quit;
                    have_pending = true;
                }
            }
        }

        // ── model output → pending result ───────────────────────────────
        // (apply() calls above returned the output; capture terminal states.)

        // ── render ───────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground(rgb(launcher_style::kBackground));

        // Even split visual divider between the left image column and the
        // right controls column.
        DrawLine(kWindowW / 2, 0, kWindowW / 2, kWindowH,
                 rgb(launcher_style::kPanelStroke));

        if (have_boxart) {
            DrawTexturePro(boxart,
                           Rectangle{0, 0, static_cast<float>(boxart.width),
                                     static_cast<float>(boxart.height)},
                           layout.boxart, Vector2{0, 0}, 0.0f, WHITE);
        } else {
            // No art: the placeholder IS the title (the activity's
            // placeholder panel — title in the muted light blue, wrapped,
            // centered on the panel).
            DrawRectangleRec(layout.boxart, rgb(launcher_style::kPanel));
            const auto title_lines = launcher_style::wrap_words(
                launcher_style::kGameTitle, layout.boxart.width - 20.0f, 28.0f,
                measure_raylib);
            const float line_h = 34.0f;
            const float block_h =
                static_cast<float>(title_lines.size()) * line_h;
            float ty = layout.boxart.y +
                       (layout.boxart.height - block_h) / 2.0f;
            for (const auto& l : title_lines) {
                const int tw = MeasureText(l.c_str(), 28);
                DrawText(l.c_str(),
                         layout.boxart.x + (layout.boxart.width - (float)tw) / 2.0f,
                         ty, 28, rgb(launcher_style::kTitlePlaceholder));
                ty += line_h;
            }
        }

        const bool ready = model.state() == LaunchScreenState::Ready;

        // The title row (the activity: config.title, white, bold).
        DrawText(launcher_style::kGameTitle, (int)(kWindowW / 2 + 20.0f), 58,
                 22, rgb(launcher_style::kTitleWhite));

        // Play button: disabled until Ready.
        if (!ready) GuiSetState(STATE_DISABLED);
        if (GuiButton(layout.play, "Play") && ready) {
            auto out = model.apply(LaunchScreenInput::PlayPressed);
            if (out == LaunchScreenOutput::Launch) {
                pending.kind = LauncherResult::Kind::Launch;
                pending.rom = model.rom_path();
                pending.bios = staged_bios;
                have_pending = true;
            }
        }
        GuiSetState(STATE_NORMAL);

        if (GuiButton(layout.select, "Select ROM")) {
            auto r = resolve_asset("", make_rom_spec(), argv0, true);
            if (r.ok) model.apply(LaunchScreenInput::RomPicked, r.path);
            else notice = r.error;
            SetWindowFocused();
        }

        // The ROM status line — the shared decision (launcher_style.h): the
        // resolved path in verified green, or the import prompt in amber.
        const float right_x = static_cast<float>(kWindowW / 2) + 20.0f;
        const launcher_style::StatusLine status =
            launcher_style::rom_status_line(ready, model.rom_path());
        DrawText(status.text.c_str(), (int)right_x, 105, (int)status.size,
                 rgb(status.color));
        if (!notice.empty()) {
            DrawText(notice.c_str(), (int)right_x, 280, 12,
                     rgb(launcher_style::kAmber));
        }

        // The privacy footnote (the activity: bottom of the controls
        // column, dimmest text, wrapped to the column).
        {
            const auto foot_lines = launcher_style::wrap_words(
                launcher_style::kPrivacyFootnote,
                static_cast<float>(kWindowW / 2) - 60.0f, 12.0f,
                measure_raylib);
            const float line_h = 15.0f;
            float fy = static_cast<float>(kWindowH) - 10.0f -
                       static_cast<float>(foot_lines.size()) * line_h;
            for (const auto& l : foot_lines) {
                DrawText(l.c_str(), (int)(kWindowW / 2 + 20.0f), (int)fy, 12,
                         rgb(launcher_style::kFootnote));
                fy += line_h;
            }
        }

        EndDrawing();
    }

    if (have_boxart) UnloadTexture(boxart);
    CloseWindow();

    if (have_pending) return pending;
    LauncherResult result;
    result.kind = LauncherResult::Kind::Quit;
    return result;
}

}  // namespace

std::unique_ptr<LauncherBackend> create_launcher_backend() {
    return std::make_unique<RayguiBackend>();
}

}  // namespace gbarecomp

#endif  // GBARECOMP_USE_RAYLIB
