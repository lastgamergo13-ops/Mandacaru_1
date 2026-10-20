// AndroidMemoryManager.cpp
// Gerenciamento de RAM adaptado ao Android.
// Monitora a pressão de memória e evita que o SO mate o processo do emulador.
//
// Integração:
//   Chame AndroidMemoryManager::Init(callback, userData) na inicialização (ver JNI_OnLoad em
//   NativeLib.cpp). Não há um Update() para chamar a partir do loop principal: todo o
//   monitoramento roda sozinho, numa thread de fundo dedicada, iniciada por Init() (ver
//   MonitorThread() abaixo).

#include "AndroidMemoryManager.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include <sys/sysinfo.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <atomic>
#include <algorithm>
#include <thread>
#include <chrono>
#include "Cemu/Logging/CemuLogging.h"

namespace AndroidMemoryManager {

static std::atomic<bool>     s_lowMemory{false};
static std::atomic<bool>     s_criticalMemory{false};
static std::atomic<uint64_t> s_availableRamMB{0};
static std::atomic<uint64_t> s_totalRamMB{0};
static std::atomic<bool>     s_monitorRunning{false};
static std::thread           s_monitorThread;

// "Memory Usage Brake" (Android: Experimental / Hackers). 0 = disabled.
static std::atomic<uint64_t> s_memoryCeilingMB{0};
static std::atomic<uint64_t> s_processRssMB{0};
static std::atomic<bool>     s_overMemoryCeiling{false};

// Callbacks para o emulador
static MemoryPressureCallback s_pressureCallback = nullptr;
static void* s_callbackUserData = nullptr;

// Limites de pressão de memória
static constexpr uint64_t LOW_MEMORY_THRESHOLD_MB      = 512;  // < 512MB → pressão baixa
static constexpr uint64_t CRITICAL_MEMORY_THRESHOLD_MB = 256;  // < 256MB → pressão crítica
static constexpr uint64_t CHECK_INTERVAL_MS            = 2000; // Verifica a cada 2 seg

// ─── Leitura de /proc/meminfo ─────────────────────────────────────────────────

struct MemInfo {
    uint64_t totalKB;
    uint64_t availableKB;
    uint64_t freeKB;
    uint64_t cachedKB;
};

static bool ReadMemInfo(MemInfo& info) {
    std::ifstream file("/proc/meminfo");
    if (!file.is_open()) return false;

    info = {};
    std::string line;
    int found = 0;

    while (std::getline(file, line) && found < 4) {
        uint64_t value = 0;
        if (line.find("MemTotal:") == 0) {
            sscanf(line.c_str(), "MemTotal: %llu kB", &value);
            info.totalKB = value;
            found++;
        } else if (line.find("MemAvailable:") == 0) {
            sscanf(line.c_str(), "MemAvailable: %llu kB", &value);
            info.availableKB = value;
            found++;
        } else if (line.find("MemFree:") == 0) {
            sscanf(line.c_str(), "MemFree: %llu kB", &value);
            info.freeKB = value;
            found++;
        } else if (line.find("Cached:") == 0) {
            sscanf(line.c_str(), "Cached: %llu kB", &value);
            info.cachedKB = value;
            found++;
        }
    }
    return found >= 2;
}

// ─── Leitura de /proc/self/status (uso de memória do PRÓPRIO processo) ────────

// VmRSS = memória física (RAM) que este processo tem residente agora. Diferente de
// /proc/meminfo acima, que descreve o dispositivo inteiro, isto descreve só o emulador -
// é o número certo para comparar contra um teto escolhido pelo usuário (ex: "pare em 4GB").
static bool ReadProcessRssMB(uint64_t& rssMB) {
    std::ifstream file("/proc/self/status");
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.find("VmRSS:") == 0) {
            uint64_t valueKB = 0;
            sscanf(line.c_str(), "VmRSS: %llu kB", &valueKB);
            rssMB = valueKB / 1024;
            return true;
        }
    }
    return false;
}

// ─── Thread de monitoramento ──────────────────────────────────────────────────

static void MonitorThread() {
    MemInfo info;
    MemoryPressureLevel lastLevel = MemoryPressureLevel::Normal;

    while (s_monitorRunning) {
        if (ReadMemInfo(info)) {
            uint64_t availMB = info.availableKB / 1024;
            uint64_t totalMB = info.totalKB / 1024;

            s_availableRamMB = availMB;
            s_totalRamMB = totalMB;

            // V11.28: limiares proporcionais a RAM total (celulares de 4-8 GB com GPU Mali sao mortos pelo
            // Android bem antes de sobrar so 512 MB). Minimo continua sendo os valores fixos antigos.
            const uint64_t lowLimitMB  = std::max<uint64_t>(LOW_MEMORY_THRESHOLD_MB, totalMB * 14 / 100);
            const uint64_t critLimitMB = std::max<uint64_t>(CRITICAL_MEMORY_THRESHOLD_MB, totalMB * 7 / 100);
            MemoryPressureLevel level;
            if (availMB < critLimitMB) {
                level = MemoryPressureLevel::Critical;
                s_lowMemory = true;
                s_criticalMemory = true;
            } else if (availMB < lowLimitMB) {
                level = MemoryPressureLevel::Low;
                s_lowMemory = true;
                s_criticalMemory = false;
            } else {
                level = MemoryPressureLevel::Normal;
                s_lowMemory = false;
                s_criticalMemory = false;
            }

            // V11.28: sob pressao de RAM do sistema, descarta texturas nao usadas automaticamente (antes so
            // acontecia com o "teto de memoria" manual ligado). Repete enquanto a pressao continuar, com intervalo.
            if (level != MemoryPressureLevel::Normal) {
                static auto lastAutoCleanup = std::chrono::steady_clock::now() - std::chrono::seconds(60);
                auto nowTp = std::chrono::steady_clock::now();
                auto minGap = std::chrono::seconds(level == MemoryPressureLevel::Critical ? 2 : 6);
                if (nowTp - lastAutoCleanup >= minGap) {
                    lastAutoCleanup = nowTp;
                    cemuLog_log(LogType::Force, "AndroidMemoryManager: pouca RAM livre ({} MB de {} MB), liberando texturas nao usadas", availMB, totalMB);
                    LatteTC_RequestAggressiveCleanup();
                }
            }

            // Notifica callback apenas quando o nível muda
            if (level != lastLevel) {
                lastLevel = level;
                cemuLog_log(LogType::Force,
                    "AndroidMemoryManager: Pressão de RAM = {} ({}/{}MB disponível)",
                    (int)level, availMB, totalMB);

                if (s_pressureCallback) {
                    s_pressureCallback(level, availMB, totalMB, s_callbackUserData);
                }
            }
        }

        // "Memory Usage Brake": independent of the system-wide check above, compare this
        // process's own RSS against the user-configured ceiling (if any).
        uint64_t ceilingMB = s_memoryCeilingMB;
        // Teto AUTOMATICO para celulares de 4-6 GB (ate 7 GB): o Android mata o processo bem antes de a RAM do aparelho acabar
        // (o sistema, o launcher e a zRAM tambem consomem), entao a limpeza preventiva comeca quando o emulador passa de ~50% da RAM
        // total - em vez de so reagir quando MemAvailable ja caiu. O teto manual do usuario (se ligado) tem prioridade.
        bool autoCeiling = false;
        if (ceilingMB == 0 && s_totalRamMB != 0 && s_totalRamMB <= 7168) {
            ceilingMB = s_totalRamMB * 50 / 100;
            autoCeiling = true;
        }
        uint64_t rssMB;
        if (ceilingMB != 0 && ReadProcessRssMB(rssMB)) {
            s_processRssMB = rssMB;
            bool isOver = rssMB >= ceilingMB;
            bool wasOver = s_overMemoryCeiling.exchange(isOver);
            if (autoCeiling && isOver && wasOver) {
                // continua acima do teto: repete o pedido de limpeza a cada ~10 s (o teto manual so pede na subida)
                static auto lastRepeat = std::chrono::steady_clock::now();
                auto nowTp = std::chrono::steady_clock::now();
                if (nowTp - lastRepeat >= std::chrono::seconds(10)) {
                    lastRepeat = nowTp;
                    LatteTC_RequestAggressiveCleanup();
                }
            }
            if (isOver && !wasOver) {
                // rising edge: just crossed the ceiling. Ask the render thread to reclaim
                // memory soon (see LatteTC_RequestAggressiveCleanup() for why this is safe to
                // call from this background thread).
                cemuLog_log(LogType::Force,
                    "AndroidMemoryManager: uso de RAM do processo ({}) MB atingiu o teto ({} MB), pedindo limpeza",
                    rssMB, ceilingMB);
                LatteTC_RequestAggressiveCleanup();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_INTERVAL_MS));
    }
}

// ─── API Pública ──────────────────────────────────────────────────────────────

void Init(MemoryPressureCallback callback, void* userData) {
    s_pressureCallback = callback;
    s_callbackUserData = userData;

    MemInfo info;
    if (ReadMemInfo(info)) {
        s_totalRamMB = info.totalKB / 1024;
        s_availableRamMB = info.availableKB / 1024;
        cemuLog_log(LogType::Force,
            "AndroidMemoryManager: Memória total = {}MB, Disponível = {}MB",
            (uint64_t)s_totalRamMB, (uint64_t)s_availableRamMB);
    }

    s_monitorRunning = true;
    s_monitorThread = std::thread(MonitorThread);

    cemuLog_log(LogType::Force, "AndroidMemoryManager: Monitor de RAM iniciado");
}

void Shutdown() {
    s_monitorRunning = false;
    if (s_monitorThread.joinable()) {
        s_monitorThread.join();
    }
}

bool IsLowMemory()      { return s_lowMemory;     }
bool IsCriticalMemory() { return s_criticalMemory; }
uint64_t GetAvailableRamMB() { return s_availableRamMB; }
uint64_t GetTotalRamMB()     { return s_totalRamMB;     }

void SetMemoryCeilingMB(uint64_t ceilingMB) {
    s_memoryCeilingMB = ceilingMB;
    if (ceilingMB == 0) {
        // feature turned off - drop any stale "over ceiling" state
        s_overMemoryCeiling = false;
    }
}
uint64_t GetMemoryCeilingMB()  { return s_memoryCeilingMB;  }
uint64_t GetProcessRssMB()     { return s_processRssMB;     }
bool     IsOverMemoryCeiling() { return s_overMemoryCeiling; }

/**
 * Ação de emergência: libera caches não essenciais do emulador.
 * Chame quando IsCriticalMemory() == true.
 */
void PerformEmergencyCleanup() {
    cemuLog_log(LogType::Force,
        "AndroidMemoryManager: LIMPEZA DE EMERGÊNCIA - RAM crítica!");

    // Pede ao motor gráfico (Latte) para descartar, já no próximo frame, toda textura que já é
    // seguro descartar - ver LatteTC_RequestAggressiveCleanup(). Isto é seguro de chamar mesmo
    // fora da thread de renderização (só marca uma flag atômica); o trabalho de fato só roda na
    // thread certa. Antes desta mudança esta função só registrava um log e não liberava memória
    // nenhuma de verdade.
    LatteTC_RequestAggressiveCleanup();

    cemuLog_log(LogType::Force,
        "AndroidMemoryManager: Emergência concluída. RAM disponível: {}MB",
        (uint64_t)s_availableRamMB);
}

/**
 * Retorna o tamanho máximo recomendado para cache de shaders
 * baseado na RAM disponível do dispositivo.
 */
uint64_t GetRecommendedShaderCacheSizeMB() {
    uint64_t totalMB = s_totalRamMB;
    if (totalMB >= 8192)     return 512;  // 8GB+ → cache de 512MB
    else if (totalMB >= 6144) return 384; // 6GB  → 384MB
    else if (totalMB >= 4096) return 256; // 4GB  → 256MB
    else if (totalMB >= 3072) return 128; // 3GB  → 128MB
    else                      return 64;  // <3GB → 64MB (economiza RAM)
}

} // namespace AndroidMemoryManager
