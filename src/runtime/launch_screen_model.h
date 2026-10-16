// launch_screen_model.h — pure launch-screen state machine (framework).
//
// Backend-agnostic: no raylib, no raygui. The raygui backend
// (launcher_raygui.cpp) drives this model and renders its state; main() sees
// only the LauncherBackend contract. Validation reuses the framework's
// gbarecomp::resolve_asset — the same call the asset picker uses, so ROM
// size/SHA-1/CRC32 rules stay in one place.
//
// States {NeedRom, Ready}; inputs {RomPicked, RomDropped, PlayPressed,
// QuitPressed}; outputs {None, PickRom, Launch, Quit}.

#pragma once

#include <string>

#include "asset_picker.h"

namespace gbarecomp {

enum class LaunchScreenState { NeedRom, Ready };
enum class LaunchScreenInput { RomPicked, RomDropped, PlayPressed, QuitPressed };
enum class LaunchScreenOutput { None, PickRom, Launch, Quit };

class LaunchScreenModel {
public:
    // rom_spec: validation rules for the ROM (size/SHA-1/CRC32).
    // argv0: executable path for the sidecar cache root.
    // staged_bios: resolved BIOS path (emibios); surfaced unchanged.
    LaunchScreenModel(AssetSpec rom_spec, std::string argv0,
                      std::string staged_bios)
        : bios_path_(std::move(staged_bios)),
          rom_spec_(rom_spec),
          argv0_(std::move(argv0)) {}

    LaunchScreenState state() const { return state_; }
    const std::string& rom_path() const { return rom_path_; }
    const std::string& bios_path() const { return bios_path_; }

    LaunchScreenOutput apply(LaunchScreenInput input,
                             const std::string& path = "") {
        switch (input) {
        case LaunchScreenInput::RomPicked:
        case LaunchScreenInput::RomDropped:
            return apply_pick(path);
        case LaunchScreenInput::PlayPressed:
            return apply_play();
        case LaunchScreenInput::QuitPressed:
            return LaunchScreenOutput::Quit;
        }
        return LaunchScreenOutput::None;
    }

private:
    LaunchScreenOutput apply_pick(const std::string& path) {
        auto r = resolve_asset(path, rom_spec_, argv0_);
        if (!r.ok) return LaunchScreenOutput::PickRom;
        rom_path_ = r.path;
        state_ = LaunchScreenState::Ready;
        return LaunchScreenOutput::None;
    }

    LaunchScreenOutput apply_play() {
        if (state_ == LaunchScreenState::Ready)
            return LaunchScreenOutput::Launch;
        return LaunchScreenOutput::None;
    }

    LaunchScreenState state_ = LaunchScreenState::NeedRom;
    std::string rom_path_;
    std::string bios_path_;
    AssetSpec rom_spec_;
    std::string argv0_;
};

}  // namespace gbarecomp
