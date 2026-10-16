// asset_picker.cpp — see asset_picker.h.

#include "asset_picker.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../gba/crc32.h"
#include "../gba/sha1.h"
#include "asset_picker_seam.h"
#include "host_platform.h"
#include "tinyfiledialogs.h"

namespace fs = std::filesystem;

namespace gbarecomp {

namespace {

std::string exe_dir_from(const std::string& argv0) {
    if (argv0.empty()) return ".";
    fs::path p(argv0);
    if (p.has_parent_path()) {
        std::error_code ec;
        auto abs = fs::absolute(p, ec);
        if (!ec) return abs.parent_path().string();
        return p.parent_path().string();
    }
    return ".";
}

std::string cache_path(const std::string& exe_dir, const char* filename) {
    return (fs::path(exe_dir) / filename).string();
}

std::string read_cache(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    return line;
}

void write_cache(const std::string& path, const std::string& asset_path) {
    std::ofstream f(path, std::ios::trunc);
    if (f) f << asset_path << "\n";
}

// Cache-entry candidates: the literal cached path, plus — on Android — the
// same path rewritten from the legacy /data/data/<pkg> form to the engine's
// /data/user/0/<pkg> form. Both name the app-private root; only the latter
// is guaranteed statable from the app's SELinux domain.
std::vector<std::string> cache_candidates(const std::string& cached) {
    std::vector<std::string> out;
    if (cached.empty()) return out;
    out.push_back(cached);
#if defined(__ANDROID__)
    static const char kLegacy[] = "/data/data/";
    static const char kModern[] = "/data/user/0/";
    if (cached.rfind(kLegacy, 0) == 0) {
        const std::string rest = cached.substr(std::strlen(kLegacy));
        const std::size_t slash = rest.find('/');
        if (slash != std::string::npos)
            out.push_back(std::string(kModern) + rest);
    }
#endif
    return out;
}

bool sha1_eq_lower(const std::string& got_hex, const char* expected) {
    if (!expected || !*expected) return true;
    std::size_t exp_len = std::strlen(expected);
    if (got_hex.size() != exp_len) return false;
    for (std::size_t i = 0; i < exp_len; ++i) {
        char a = static_cast<char>(std::tolower(
            static_cast<unsigned char>(got_hex[i])));
        char b = static_cast<char>(std::tolower(
            static_cast<unsigned char>(expected[i])));
        if (a != b) return false;
    }
    return true;
}

std::string hex32(std::uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08X", v);
    return std::string(buf);
}

// tinyfiledialogs (vendored, zlib) provides native dialogs on desktop: Win32
// GetOpenFileNameA/MessageBoxA underneath, NSOpenPanel on macOS, and
// zenity/kdialog/matedialog on Linux.
//
// ANDROID: tinyfd has no backend there — and its no-backend path is not
// harmless: detection prints a "missing software!" banner and blocks on a
// "press enter to continue" stdin read, once per dialog attempt (observed on
// device, 2026-10-16). The wrappers below therefore never reach tinyfd on
// Android: a pick fails cleanly with a log line. Android's picker is the
// platform's own setup activity (GbaSetupActivity) — the pure-Android
// pre-boot surface, not this seam.
#include "tinyfiledialogs.h"

std::string pick_with_dialog_impl(const AssetSpec& spec) {
    std::vector<std::string> pattern_storage;
    std::vector<const char*> patterns;
    std::string description = "All Files";
    if (spec.dialog_filter && *spec.dialog_filter) {
        // spec.dialog_filter is a double-null-terminated "desc1\0pat1\0desc2\0pat2\0"
        // list. Split into (description, pattern...) for tinyfd, which takes a
        // single description plus a pattern array.
        const char* p = spec.dialog_filter;
        while (*p) {
            std::string desc = p;
            p += desc.size() + 1;
            std::string pat = p;
            p += pat.size() + 1;
            description = std::move(desc);
            pattern_storage.push_back(std::move(pat));
        }
    }
    for (const auto& pat : pattern_storage) patterns.push_back(pat.c_str());
    patterns.push_back(nullptr);

    const char* picked = tinyfd_openFileDialog(
        spec.dialog_title ? spec.dialog_title : spec.display_name,
        nullptr, static_cast<int>(patterns.size()) - 1, patterns.data(),
        description.c_str(), 0);
    return picked ? std::string(picked) : std::string();
}

void show_dialog_impl(const char* title, const std::string& body, unsigned flags) {
    const char* icon = (flags == kAssetDialogWarning) ? "warning" : "error";
    tinyfd_messageBox(title, body.c_str(), "ok", icon, 1);
}

// Seam-checked wrappers: tests override asset_picker_seam::pick_with_dialog /
// show_dialog to inject behavior; production leaves them null and falls back
// to the real platform implementations above.
std::string pick_with_dialog(const AssetSpec& spec) {
    if (asset_picker_seam::pick_with_dialog)
        return asset_picker_seam::pick_with_dialog(spec);
#if defined(__ANDROID__)
    // No desktop dialog backends exist here, and tinyfd's fallback prints a
    // banner and reads stdin (see the include-site note). The platform layer
    // installs the SAF seam; without it, fail cleanly.
    std::fprintf(stderr,
                 "[asset_picker] no ROM picker installed (Android SAF seam "
                 "missing); stage the asset in the payload or pass --rom.\n");
    return {};
#else
    // Avoid tinyfd's blocking console-input fallback on systems lacking a
    // graphical chooser: that fallback reads from stdin and freezes the GUI.
    char* has_gui = tinyfd_openFileDialog("tinyfd_query", nullptr, 0, nullptr,
                                         nullptr, 0);
    if (has_gui == nullptr) {
        show_dialog_impl("Select ROM",
                    "No graphical file chooser (zenity/kdialog) is available on "
                    "this system. Pass the ROM with --rom instead.",
                    kAssetDialogWarning);
        return {};
    }
    return pick_with_dialog_impl(spec);
#endif
}

void show_dialog(const char* title, const std::string& body, unsigned flags) {
    if (asset_picker_seam::show_dialog)
        return asset_picker_seam::show_dialog(title, body, flags);
#if defined(__ANDROID__)
    // Same Android rule: no tinyfd. Dialog-grade messaging becomes a log line.
    std::fprintf(stderr, "[asset_picker] %s: %s\n", title, body.c_str());
#else
    show_dialog_impl(title, body, flags);
#endif
}


AssetResult load_and_validate(const std::string& path,
                              const AssetSpec& spec) {
    AssetResult r;
    r.path = path;
    std::string err;
    r.bytes = read_file(path, &err);
    if (!err.empty() || r.bytes.empty()) {
        r.error = err.empty() ? ("read failed: " + path) : err;
        return r;
    }
    if (spec.expected_size != 0 && r.bytes.size() != spec.expected_size) {
        std::ostringstream oss;
        oss << spec.display_name << " size mismatch: got "
            << r.bytes.size() << " bytes, expected "
            << spec.expected_size << " bytes.";
        r.error = oss.str();
        return r;
    }
    r.sha1_hex = gba::sha1(r.bytes.data(), r.bytes.size()).hex();
    r.crc32 = gba::crc32(r.bytes.data(), r.bytes.size());

    const bool sha_ok = sha1_eq_lower(r.sha1_hex, spec.expected_sha1);
    const bool crc_ok = (spec.expected_crc32 == 0) ||
                        (spec.expected_crc32 == r.crc32);

    if (!sha_ok || !crc_ok) {
        std::ostringstream w;
        w << spec.display_name << " hash mismatch:\n";
        w << "  SHA-1 got " << r.sha1_hex;
        if (spec.expected_sha1 && *spec.expected_sha1) {
            w << "\n        expected " << spec.expected_sha1;
        }
        w << "\n  CRC32 got " << hex32(r.crc32);
        if (spec.expected_crc32 != 0) {
            w << "\n        expected " << hex32(spec.expected_crc32);
        }
        if (spec.hash_mismatch_is_error) {
            r.error = w.str();
            return r;
        }
        w << "\n\nThis is not the recompiled-against image. Behavior "
             "is undefined; proceeding anyway.";
        r.warning = w.str();
    }
    r.ok = true;
    return r;
}

}  // namespace

std::string launcher_state_path(const std::string& argv0,
                                const char* filename) {
    return cache_path(exe_dir_from(argv0), filename);
}

AssetResult resolve_asset(const std::string& argv_path,
                          const AssetSpec& spec,
                          const std::string& argv0,
                          bool force_pick) {
    const std::string exe_dir = exe_dir_from(argv0);
    const std::string cache = cache_path(exe_dir, spec.cache_filename);

    // 1. Explicit argv path: if it loads and validates, take it (cache
    //    for next launch). If it doesn't even load (missing file / bad
    //    size), fall through to the cached path and the picker — the
    //    user may have typed the wrong path or be on a release build
    //    where the dev default doesn't exist.
    if (!argv_path.empty()) {
        std::error_code ec;
        if (fs::exists(argv_path, ec) && !ec) {
            auto r = load_and_validate(argv_path, spec);
            if (r.ok) {
                if (spec.persist_cache) write_cache(cache, argv_path);
                return r;
            }
            // Wrong size or unreadable. Fall through to the cached path and
            // the picker — the user may have typed the wrong path or be on a
            // release build where the dev default doesn't exist.
        }
    }

    // 2. Cached path from a prior successful pick. On Android the cached
    //    string may carry the legacy /data/data/<pkg> private-dir form while
    //    the engine's own view is /data/user/0/<pkg> — that stat can fail
    //    silently (dontaudit'd on some vendor ROMs) and strand a perfectly
    //    good cache entry, so each candidate form is tried before the
    //    picker is considered.
    if (!force_pick && spec.persist_cache) {
        const std::string cached = read_cache(cache);
        for (const std::string& candidate : cache_candidates(cached)) {
            std::error_code ec;
            if (fs::exists(candidate, ec) && !ec) {
                auto r = load_and_validate(candidate, spec);
                if (r.ok) return r;
            }
        }
    }

    while (true) {
        std::string picked = pick_with_dialog(spec);
        if (picked.empty()) {
            AssetResult r;
            r.error = std::string(spec.display_name) +
                      ": no file selected (dialog cancelled).";
            return r;
        }
        auto r = load_and_validate(picked, spec);
        if (!r.ok) {
            show_dialog(spec.display_name,
                        r.error + "\n\nPlease pick a different file.",
                        kAssetDialogError);
            continue;
        }
        if (!r.warning.empty()) {
            show_dialog(spec.display_name, r.warning, kAssetDialogWarning);
        }
        if (spec.persist_cache) write_cache(cache, picked);
        return r;
    }
}

AssetResult validate_asset_path(const std::string& path,
                                const AssetSpec& spec) {
    if (path.empty()) {
        AssetResult r;
        r.error = std::string(spec.display_name) + ": no file selected.";
        return r;
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        AssetResult r;
        r.path = path;
        r.error = std::string(spec.display_name) +
                  ": file is missing or is not a regular file.";
        return r;
    }
    return load_and_validate(path, spec);
}

}  // namespace gbarecomp
