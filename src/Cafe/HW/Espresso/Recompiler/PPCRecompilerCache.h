#pragma once

#include "PPCRecompiler.h"
#include "PPCFunctionBoundaryTracker.h"
#include "Cafe/TitleList/TitleId.h"
#include <string>

// AOT (ahead-of-time) disk cache for the PPC recompiler.
//
// Background / why this exists:
// Recompiling PPC code to native machine code on every single game boot is one of the
// biggest sources of stutter and slow startup on Android, where CPU cores are slower and
// more power constrained than on desktop. This cache persists already-translated functions
// to disk (one file per title, mirroring how LatteShaderCache.cpp caches shaders) so that on
// a later launch of the same title, previously compiled functions can be injected directly
// instead of being fully retranslated.
//
// IMPORTANT safety note: the machine code produced by both JIT backends is NOT position
// independent. Every call to an HLE function embeds an absolute host pointer directly into
// the instruction stream (see PPCRecRelocEntry in PPCRecompiler.h), and those pointers are
// only valid for the process run that generated them (ASLR randomizes them on every launch).
// This cache always re-patches every recorded relocation against the *current* run's actual
// addresses before a cached function is handed back - never replay raw cached bytes as-is.
//
// The cache additionally guards against stale/mismatched entries: every entry stores a CRC32
// of the original PPC instruction bytes it was compiled from, and a cache hit is only
// considered valid if that checksum still matches the game's current memory contents. This
// means a patched/updated version of a game (or a different version of Cemu that changed how
// code is generated) will simply be treated as a cache miss and recompiled normally, rather
// than risking execution of stale or incompatible code.
//
// V11.55: the guest-code CRC only protects against GUEST changes. The native code itself depends on the exact build
// (struct offsets, backend conventions), so the file's extra-version now also includes the GNU build-id of the shared
// library: every rebuild of the app starts with an empty AOT cache instead of running machine code from an older build.

// Loads (or lazily creates) the on-disk AOT cache file for the given title.
// Must be called after PPCRecompiler_init() has set up the recompiler for the currently
// booting title, and before any PPC code is recompiled for it.
void PPCRecompilerCache_Load(TitleId titleId);

// Flushes and closes the currently open AOT cache file, if any. Safe to call even if
// PPCRecompilerCache_Load() was never called or already failed.
void PPCRecompilerCache_Unload();

// Attempts to satisfy a recompilation request from the on-disk cache.
// On a cache hit, returns a fully formed PPCRecFunction_t - with all embedded absolute
// addresses already re-patched for this run - plus its list of entry points (ppc address,
// code offset) ready to be passed to PPCRecompiler_makeRecompiledFunctionActive(), exactly as
// with a freshly compiled function. Returns nullptr on a cache miss (including: no cache open,
// AOT cache disabled, entry not found, format/version/architecture mismatch, or the guest code
// bytes no longer match the checksum recorded at compile time).
PPCRecFunction_t* PPCRecompilerCache_TryLoad(const PPCFunctionBoundaryTracker::PPCRange_t& range, std::vector<std::pair<MPTR, uint32>>& entryPointsOut);

// Persists a freshly compiled function (and its entry points) to the on-disk cache.
// This is fire-and-forget; the write happens on a background thread and never blocks
// recompilation. Safe to call even if no cache is currently open (no-op in that case).
void PPCRecompilerCache_Store(PPCRecFunction_t* ppcRecFunc, const std::vector<std::pair<MPTR, uint32>>& entryPoints);

// Resumo dos contadores do cache AOT (acertos, falhas, rejeicoes, gravacoes) numa linha de texto pronta para o log.
// So le contadores atomicos: pode ser chamada de qualquer thread, inclusive do relatorio de crash.
std::string PPCRecompilerCache_GetStatsString();
