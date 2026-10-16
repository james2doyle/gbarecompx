// test_launch_screen_model.cpp — launch-screen state model test.
//
// Pure state machine, no raylib. States {NeedRom, Ready}; inputs
// {RomPicked, RomDropped, PlayPressed, QuitPressed}; outputs {None, PickRom,
// Launch, Quit}.
//
// Rules under test:
//   - fresh repo dir (no rom.cfg) starts NeedRom
//   - RomPicked/RomDropped validate via gbarecomp::resolve_asset (real
//     framework call) and transition to Ready on success
//   - PlayPressed in NeedRom is ignored (button disabled in the view)
//   - PlayPressed in Ready -> Launch; QuitPressed -> Quit
//   - invalid pick -> PickRom (view re-opens the picker), stays NeedRom
//
// The asset_picker_seam is injected so resolve_asset's dialog loop gets an
// instant cancel (no real tinyfd dialog / stdin block in headless CI).

#include <cstdio>
#include <cstring>
#include <string>

#include "asset_picker_seam.h"
#include "launch_screen_model.h"

namespace {

constexpr const char* kRomPath = "launch_model_test.gba";

gbarecomp::AssetSpec make_rom_spec() {
    gbarecomp::AssetSpec spec;
    spec.display_name = "test ROM";
    spec.dialog_filter = "GBA ROM (*.gba)\0*.gba\0";
    spec.dialog_title = "Select ROM";
    spec.cache_filename = "launch_model_test.cfg";
    spec.expected_size = 0;  // any size accepted
    spec.expected_sha1 = nullptr;
    spec.expected_crc32 = 0;
    spec.persist_cache = false;
    return spec;
}

// Seam: dialog returns empty (cancel) so resolve_asset's dialog loop exits
// instantly without opening a real OS dialog or blocking on stdin.
std::string fake_pick(const gbarecomp::AssetSpec&) { return {}; }
void fake_show(const char*, const std::string&, unsigned) {}

void install_seam() {
    gbarecomp::asset_picker_seam::pick_with_dialog = &fake_pick;
    gbarecomp::asset_picker_seam::show_dialog = &fake_show;
}

void write_rom_file() {
    std::FILE* f = std::fopen(kRomPath, "wb");
    if (!f) {
        std::fprintf(stderr, "cannot create %s\n", kRomPath);
        std::exit(1);
    }
    const char bytes[] = "launch-model test ROM (not a real cartridge)";
    std::fwrite(bytes, 1, sizeof(bytes) - 1, f);
    std::fclose(f);
}

void remove_artifacts() {
    std::remove(kRomPath);
    std::remove("launch_model_test.cfg");
}

}  // namespace

int run_launch_screen_model_tests() {
    int failures = 0;
    remove_artifacts();
    write_rom_file();
    install_seam();

    // Fresh model starts NeedRom.
    {
        gbarecomp::LaunchScreenModel model(make_rom_spec(), ".",
                                           "staged_bios.bin");
        if (model.state() != gbarecomp::LaunchScreenState::NeedRom) {
            std::printf("model FAIL: fresh state != NeedRom\n");
            ++failures;
        } else {
            std::printf("model: fresh -> NeedRom OK\n");
        }
    }

    // PlayPressed in NeedRom is ignored.
    {
        gbarecomp::LaunchScreenModel model(make_rom_spec(), ".",
                                           "staged_bios.bin");
        auto out = model.apply(gbarecomp::LaunchScreenInput::PlayPressed);
        if (out != gbarecomp::LaunchScreenOutput::None ||
            model.state() != gbarecomp::LaunchScreenState::NeedRom) {
            std::printf("model FAIL: PlayPressed in NeedRom not ignored\n");
            ++failures;
        } else {
            std::printf("model: PlayPressed in NeedRom ignored OK\n");
        }
    }

    // RomPicked(valid) -> Ready, rom_path set.
    {
        gbarecomp::LaunchScreenModel model(make_rom_spec(), ".",
                                           "staged_bios.bin");
        auto out = model.apply(gbarecomp::LaunchScreenInput::RomPicked,
                               kRomPath);
        if (out != gbarecomp::LaunchScreenOutput::None ||
            model.state() != gbarecomp::LaunchScreenState::Ready ||
            model.rom_path() != kRomPath) {
            std::printf("model FAIL: RomPicked(valid) != Ready\n");
            ++failures;
        } else {
            std::printf("model: RomPicked(valid) -> Ready OK\n");
        }
    }

    // RomDropped(valid) behaves like RomPicked.
    {
        gbarecomp::LaunchScreenModel model(make_rom_spec(), ".",
                                           "staged_bios.bin");
        auto out = model.apply(gbarecomp::LaunchScreenInput::RomDropped,
                               kRomPath);
        if (out != gbarecomp::LaunchScreenOutput::None ||
            model.state() != gbarecomp::LaunchScreenState::Ready) {
            std::printf("model FAIL: RomDropped(valid) != Ready\n");
            ++failures;
        } else {
            std::printf("model: RomDropped(valid) -> Ready OK\n");
        }
    }

    // PlayPressed in Ready -> Launch.
    {
        gbarecomp::LaunchScreenModel model(make_rom_spec(), ".",
                                           "staged_bios.bin");
        model.apply(gbarecomp::LaunchScreenInput::RomPicked, kRomPath);
        auto out = model.apply(gbarecomp::LaunchScreenInput::PlayPressed);
        if (out != gbarecomp::LaunchScreenOutput::Launch) {
            std::printf("model FAIL: PlayPressed in Ready != Launch\n");
            ++failures;
        } else {
            std::printf("model: PlayPressed in Ready -> Launch OK\n");
        }
    }

    // QuitPressed -> Quit (from either state).
    {
        gbarecomp::LaunchScreenModel model(make_rom_spec(), ".",
                                           "staged_bios.bin");
        auto out = model.apply(gbarecomp::LaunchScreenInput::QuitPressed);
        if (out != gbarecomp::LaunchScreenOutput::Quit) {
            std::printf("model FAIL: QuitPressed != Quit\n");
            ++failures;
        } else {
            std::printf("model: QuitPressed -> Quit OK\n");
        }
    }

    // Invalid pick -> PickRom, stays NeedRom.
    {
        gbarecomp::LaunchScreenModel model(make_rom_spec(), ".",
                                           "staged_bios.bin");
        auto out = model.apply(gbarecomp::LaunchScreenInput::RomPicked,
                               "/nonexistent/path/to/rom.gba");
        if (out != gbarecomp::LaunchScreenOutput::PickRom ||
            model.state() != gbarecomp::LaunchScreenState::NeedRom) {
            std::printf("model FAIL: RomPicked(invalid) != PickRom\n");
            ++failures;
        } else {
            std::printf("model: RomPicked(invalid) -> PickRom OK\n");
        }
    }

    // Staged bios path is surfaced unchanged.
    {
        gbarecomp::LaunchScreenModel model(make_rom_spec(), ".",
                                           "staged_bios.bin");
        if (model.bios_path() != "staged_bios.bin") {
            std::printf("model FAIL: bios_path mismatch\n");
            ++failures;
        } else {
            std::printf("model: bios_path surfaced OK\n");
        }
    }

    remove_artifacts();
    if (failures == 0) {
        std::printf("model: all state-machine cases pass\n");
    }
    return failures;
}

int main() {
    return run_launch_screen_model_tests() == 0 ? 0 : 1;
}
