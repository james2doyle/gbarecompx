// asset_picker_test.cpp — RED-first dialog-seam test (Step 2.1).
//
// Drives gbarecomp::resolve_asset with no argv path and no sidecar cache, with
// the dialog injected through the asset_picker_seam function pointers. Asserts:
//   (a) the dialog is offered on every platform,
//   (b) a picked path is validated and cached,
//   (c) cancel surfaces the "no file selected" error,
//   (d) a hash mismatch warns and proceeds.
//
// RED: written against the target seam shape; does not compile until Step 2.2
// adds asset_picker_seam.h. On non-Windows it also fails at runtime today
// because resolve_asset early-returns before offering a dialog.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "asset_picker.h"
#include "asset_picker_seam.h"

namespace {

// A real on-disk file so load_and_validate succeeds; its hash won't match the
// spec's expected_sha1, exercising the warn-and-proceed path.
constexpr const char* kRomPath = "rom_seam_test.gba";
std::string g_injected_path;
bool g_dialog_offered = false;
bool g_warned = false;

std::string fake_pick(const gbarecomp::AssetSpec&) {
    g_dialog_offered = true;
    return g_injected_path;
}

void fake_show(const char*, const std::string& body, unsigned) {
    g_warned = true;
    std::printf("warn: %s\n", body.c_str());
}

void reset_seam(const std::string& path) {
    g_injected_path = path;
    g_dialog_offered = false;
    g_warned = false;
    gbarecomp::asset_picker_seam::pick_with_dialog = &fake_pick;
    gbarecomp::asset_picker_seam::show_dialog = &fake_show;
}

}  // namespace

int main() {
    // Remove stale artifacts from any previous crashed run.
    std::remove(kRomPath);
    std::remove("rom_seam_test.cfg");

    // Create the backing file the injected path points at.
    {
        std::FILE* f = std::fopen(kRomPath, "wb");
        if (!f) {
            std::fprintf(stderr, "cannot create %s\n", kRomPath);
            return 1;
        }
        const char bytes[] = "FFTA seam test ROM (not a real cartridge)";
        std::fwrite(bytes, 1, sizeof(bytes) - 1, f);
        std::fclose(f);
    }

    gbarecomp::AssetSpec spec;
    spec.display_name = "test ROM";
    spec.dialog_filter = "GBA ROM (*.gba)\0*.gba\0";
    spec.dialog_title = "Select ROM";
    spec.cache_filename = "rom_seam_test.cfg";
    spec.expected_size = 0;
    spec.expected_sha1 = "0000000000000000000000000000000000000000";
    spec.expected_crc32 = 0;
    spec.persist_cache = false;

    // (a)+(b): dialog offered, picked path validated and cached.
    {
        reset_seam(kRomPath);
        auto r = gbarecomp::resolve_asset("", spec, ".");
        assert(g_dialog_offered);
        assert(r.ok);
        assert(r.path == kRomPath);
        // (d) hash mismatch warns and proceeds (expected_sha1 is all zeros).
        assert(g_warned);
        assert(!r.warning.empty());
        std::printf("asset_picker: dialog offered + picked path + warn OK\n");
    }

    // (c): cancel surfaces the "no file selected" error.
    {
        reset_seam("");
        auto r = gbarecomp::resolve_asset("", spec, ".");
        assert(g_dialog_offered);
        assert(!r.ok);
        assert(r.error.find("no file selected") != std::string::npos);
        std::printf("asset_picker: cancel -> error OK\n");
    }

    // (e): repick bypasses a valid sidecar cache only when requested.
    {
        // Re-bind a spec that persists its pick beside the executable.
        gbarecomp::AssetSpec cspec = spec;
        cspec.persist_cache = true;

        const char* kCache = "rom_seam_test.cfg";
        const char* kCachedRom = kRomPath;          // cached candidate
        const char* kRepickedRom = "rom_seam_repicked.gba";

        // Prepare two readable files so both the cached and repicked picks
        // validate.
        {
            std::FILE* f = std::fopen(kCachedRom, "wb");
            if (!f) { std::fprintf(stderr, "cannot create %s\n", kCachedRom); return 1; }
            const char bytes[] = "cached ROM payload";
            std::fwrite(bytes, 1, sizeof(bytes) - 1, f);
            std::fclose(f);
            std::FILE* g = std::fopen(kRepickedRom, "wb");
            if (!g) { std::fprintf(stderr, "cannot create %s\n", kRepickedRom); return 1; }
            const char bytes2[] = "repicked ROM payload";
            std::fwrite(bytes2, 1, sizeof(bytes2) - 1, g);
            std::fclose(g);
        }

        // Seed the cache to point at the cached candidate.
        {
            std::ofstream cf(kCache, std::ios::trunc);
            if (!cf) { std::fprintf(stderr, "cannot write %s\n", kCache); return 1; }
            cf << kCachedRom << "\n";
        }

        // Without forcing, the cache wins and the dialog is NOT offered.
        // Reset the seam globals so we observe any dialog calls.
        reset_seam(kCachedRom);
        gbarecomp::asset_picker_seam::pick_with_dialog = &fake_pick;
        gbarecomp::asset_picker_seam::show_dialog = &fake_show;
        g_dialog_offered = false;
        auto cached = gbarecomp::resolve_asset("", cspec, ".", false);
        assert(cached.ok);
        assert(cached.path == kCachedRom);
        assert(!g_dialog_offered && "a valid cache must not prompt");

        // Forcing a pick must bypass the cache and call the dialog.
        reset_seam(kRepickedRom);
        gbarecomp::asset_picker_seam::pick_with_dialog = &fake_pick;
        gbarecomp::asset_picker_seam::show_dialog = &fake_show;
        g_dialog_offered = false;
        auto repicked = gbarecomp::resolve_asset("", cspec, ".", true);
        assert(g_dialog_offered);
        assert(repicked.ok);
        assert(repicked.path == kRepickedRom);

        std::remove(kCachedRom);
        std::remove(kRepickedRom);
        std::remove(kCache);
        std::printf("asset_picker: cache hit + forced repick OK\n");
    }

    std::remove(kRomPath);
    std::remove("rom_seam_test.cfg");
    std::printf("asset_picker: all dialog-seam cases pass\n");
    return 0;
}
