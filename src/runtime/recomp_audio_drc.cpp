// The single translation unit that carries the RAB implementation.
//
// recomp_audio_drc.h is a single-file component: exactly ONE translation unit
// defines RECOMP_AUDIO_DRC_IMPL. That unit used to be host_window.cpp, which
// worked but inverted the layering -- a pure, platform-free DSP component (RAB:
// a resampler plus a ring buffer, zero SDL, zero raylib) was compiled into a
// windowing translation unit, so anything that merely wanted the audio code had
// to link the entire host window and with it SDL, ImGui and the generated
// dispatch table.
//
// That coupling made the raylib backend untestable in isolation and therefore
// untestable in CI: the ROM-derived generated sources the dispatch table needs
// are never committed, so a test that links gbarecomp_runtime cannot even link
// on a fresh checkout. Compiled into its own tiny static library instead, both
// backends get RAB by linking one archive member, and the raylib integration
// test needs neither the ROM nor SDL.
//
// Keep this file to the include and nothing else -- any code here would be
// compiled into every consumer of the audio path.
#define RECOMP_AUDIO_DRC_IMPL
#include "recomp_audio_drc.h"