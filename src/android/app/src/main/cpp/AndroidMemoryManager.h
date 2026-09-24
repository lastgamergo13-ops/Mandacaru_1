#pragma once
// AndroidMemoryManager.h

#include <cstdint>

namespace AndroidMemoryManager {

enum class MemoryPressureLevel {
    Normal   = 0,  // > 512MB disponível
    Low      = 1,  // 256-512MB disponível
    Critical = 2   // < 256MB disponível
};

// Callback chamado quando o nível de pressão de memória muda
using MemoryPressureCallback = void(*)(
    MemoryPressureLevel level,
    uint64_t availableMB,
    uint64_t totalMB,
    void* userData
);

void      Init(MemoryPressureCallback callback = nullptr, void* userData = nullptr);
void      Shutdown();

bool      IsLowMemory();
bool      IsCriticalMemory();
uint64_t  GetAvailableRamMB();
uint64_t  GetTotalRamMB();

// User-configured process memory ceiling ("Memory Usage Brake", Android: Experimental /
// Hackers). This is independent of the system-wide available-RAM tracking above: it tracks how
// much memory THIS process itself is using (RSS) against a budget the user picked, rather than
// how much RAM the whole device has free. 0 = disabled (the ceiling is never checked).
void      SetMemoryCeilingMB(uint64_t ceilingMB);
uint64_t  GetMemoryCeilingMB();
uint64_t  GetProcessRssMB();
bool      IsOverMemoryCeiling();

void      PerformEmergencyCleanup();
uint64_t  GetRecommendedShaderCacheSizeMB();

} // namespace AndroidMemoryManager
