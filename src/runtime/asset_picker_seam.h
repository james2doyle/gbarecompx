// asset_picker_seam.h — test seam for the asset picker dialogs.
//
// Production leaves both pointers null, so resolve_asset falls back to the real
// platform implementations (Win32 GetOpenFileNameA / MessageBoxA, or the
// tinyfiledialogs path added in Phase 2). Unit tests override them to inject
// dialog behavior without a display.

#pragma once

#include <string>

#include "asset_picker.h"

namespace gbarecomp {
namespace asset_picker_seam {

inline std::string (*pick_with_dialog)(const AssetSpec&) = nullptr;
inline void (*show_dialog)(const char*, const std::string&, unsigned) = nullptr;

}  // namespace asset_picker_seam
}  // namespace gbarecomp
