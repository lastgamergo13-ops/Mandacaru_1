package info.cemu.cemu.nativeinterface

object NativeSettings {
    @JvmStatic
    external fun saveSettings()

    @JvmStatic
    external fun addGamesPath(uri: String?)

    @JvmStatic
    external fun removeGamesPath(uri: String?)

    @JvmStatic
    external fun getGamesPaths(): Array<String>

    @JvmStatic
    external fun getAsyncShaderCompile(): Boolean

    @JvmStatic
    external fun setAsyncShaderCompile(value: Boolean)

    // Recompiler AOT (ahead-of-time) disk cache: persists compiled PPC functions per
    // title so a later launch of the same title can reuse them instead of recompiling
    // from scratch.
    @JvmStatic
    external fun getRecompilerAOTCache(): Boolean

    @JvmStatic
    external fun setRecompilerAOTCache(value: Boolean)

    // Limitar threads do compilador de shaders: caps both shader/pipeline compiler thread pools
    // at 2 threads instead of up to 8, to avoid RAM-spike OOM kills on Android. Only takes
    // effect on the next title boot.
    @JvmStatic
    external fun getLimitShaderCompilerThreads(): Boolean

    @JvmStatic
    external fun setLimitShaderCompilerThreads(value: Boolean)

    // FSR3 Frame Generation (Vulkan only). Takes effect immediately, even mid-game.
    @JvmStatic
    external fun getFsr3FrameGen(): Boolean

    @JvmStatic
    external fun setFsr3FrameGen(value: Boolean)

    // Diagnostico Vulkan (sync por submit + rastreio de pipelines). Resultado em vk_trace.txt.
    @JvmStatic
    external fun getVkSyncDebug(): Boolean

    /** Chaves de "Logs e Diagnostico" (ver DiagFlag). */
    object DiagFlag {
        const val GL_DEBUG = 0
        const val TEX_LOG = 1
        const val AUTO_CLEAN_OFF = 2
        const val SUSTAINED_PERFORMANCE = 3
        const val DUMP_SHADERS = 4
        const val JIT_STRICT_ORDERING = 5
        const val THREAD_START_DELAY = 6
        const val GUEST_WATCH = 7
        const val AUDIO_BOOST = 8
        const val PREFER_BIG_CORES = 9
        const val REDUCE_GAME_MEM = 10 // 128 MB
        const val REDUCE_MEM_64 = 11
        const val REDUCE_MEM_256 = 12
        const val REDUCE_MEM_384 = 13
        const val REDUCE_MEM_512 = 14
        const val JIT_BISECT_1 = 15 // 02000000-07000000
        const val JIT_BISECT_2 = 16 // 02030000-020A0000
        const val JIT_BISECT_3 = 17 // 02030000-02070000
        const val JIT_BISECT_4 = 18 // 02070000-020A0000
        const val JIT_BISECT_5 = 19 // 02030000-02050000
        const val JIT_BISECT_6 = 20 // 02050000-02070000
        const val JIT_BISECT_7 = 21 // 02070000-02080000
        const val JIT_BISECT_8 = 22 // 02080000-020A0000
        const val QUANTUM_20K = 23
        const val QUANTUM_10K = 24
        const val QUANTUM_5K = 25
        const val QUANTUM_2K = 26
    }

    @JvmStatic
    external fun getDiagFlag(flag: Int): Boolean

    @JvmStatic
    external fun setDiagFlag(flag: Int, enabled: Boolean)

    /** Apaga vk_quarantine*.txt e gl_shader_quarantine.txt. Devolve quantos arquivos foram apagados. */
    @JvmStatic
    external fun deleteQuarantineFiles(): Int

    @JvmStatic
    external fun setVkSyncDebug(value: Boolean)

    // Opcoes do FSR3 Frame Gen / FSR Upscale (valem para todos os jogos, salvo perfil do jogo).
    // key: 0 frame gen (0/1) | 1 multiplicador (2..4) | 2 flow scale % (50/75/100)
    //      3 modo performance (0/1) | 4 limite de FPS base (0/30/40/60) | 5 FSR upscale (0/1)
    object Fsr3Option {
        const val FRAME_GEN = 0
        const val MULTIPLIER = 1
        const val FLOW_SCALE = 2
        const val PERFORMANCE_MODE = 3
        const val BASE_FPS_LIMIT = 4
        const val UPSCALE = 5
        const val OVERLAY_SHOW = 6
        const val OVERLAY_DECIMALS = 7
        const val OVERLAY_STYLE = 8
        const val OVERLAY_GPU_MS = 9
        const val PYRAMID = 10
    }

    @JvmStatic
    external fun getFsr3Option(key: Int): Int

    @JvmStatic
    external fun setFsr3Option(key: Int, value: Int)

    object VSyncMode {
        const val OFF: Int = 0
        const val DOUBLE_BUFFERING: Int = 1
        const val TRIPLE_BUFFERING: Int = 2
    }

    @JvmStatic
    external fun getVsyncMode(): Int

    @JvmStatic
    external fun setVsyncMode(value: Int)

    // Render API (Experimental/Hackers tab): which graphics backend the emulator uses.
    // AUTOMATIC: the emulator picks Vulkan or OpenGL ES for each game when it is launched
    // (and tries the other one if the first fails to initialize). VULKAN / OPENGL: only that
    // API - if it fails to initialize, the game does not start. See the native
    // NativeEmulation_initializeRenderer.
    object RendererMode {
        const val AUTOMATIC: Int = 0
        const val VULKAN: Int = 1
        const val OPENGL: Int = 2
    }

    @JvmStatic
    external fun getRendererMode(): Int

    @JvmStatic
    external fun setRendererMode(value: Int)

    object FullscreenScaling {
        const val KEEP_ASPECT_RATIO: Int = 0
        const val STRETCH: Int = 1
    }

    @JvmStatic
    external fun getFullscreenScaling(): Int

    @JvmStatic
    external fun setFullscreenScaling(value: Int)

    object ScalingFilter {
        const val BILINEAR_FILTER: Int = 0
        const val BICUBIC_FILTER: Int = 1
        const val BICUBIC_HERMITE_FILTER: Int = 2
        const val NEAREST_NEIGHBOR_FILTER: Int = 3
        const val FSR_FILTER: Int = 4
    }

    @JvmStatic
    external fun getUpscalingFilter(): Int

    @JvmStatic
    external fun setUpscalingFilter(value: Int)

    @JvmStatic
    external fun getDownscalingFilter(): Int

    @JvmStatic
    external fun setDownscalingFilter(value: Int)

    // FSR RCAS sharpening amount, in "stops" (0.0 = sharpest, higher = softer) - only
    // meaningful when the upscale filter is set to ScalingFilter.FSR_FILTER above.
    @JvmStatic
    external fun getFsrSharpness(): Float

    @JvmStatic
    external fun setFsrSharpness(value: Float)

    @JvmStatic
    external fun getAccurateBarriers(): Boolean

    @JvmStatic
    external fun setAccurateBarriers(value: Boolean)

    // Experimental per-format texture compression (BC1-BC5 -> ETC2/EAC on GPUs lacking
    // native BC/S3TC support). One getter/setter pair per format so the user can choose
    // exactly which transcoders to opt into.
    @JvmStatic
    external fun getTextureCompressionBC1(): Boolean

    @JvmStatic
    external fun setTextureCompressionBC1(value: Boolean)

    @JvmStatic
    external fun getTextureCompressionBC2(): Boolean

    @JvmStatic
    external fun setTextureCompressionBC2(value: Boolean)

    @JvmStatic
    external fun getTextureCompressionBC3(): Boolean

    @JvmStatic
    external fun setTextureCompressionBC3(value: Boolean)

    @JvmStatic
    external fun getTextureCompressionBC4(): Boolean

    @JvmStatic
    external fun setTextureCompressionBC4(value: Boolean)

    @JvmStatic
    external fun getTextureCompressionBC5(): Boolean

    @JvmStatic
    external fun setTextureCompressionBC5(value: Boolean)

    @JvmStatic
    external fun getAudioDeviceEnabled(tv: Boolean): Boolean

    @JvmStatic
    external fun setAudioDeviceEnabled(enabled: Boolean, tv: Boolean)

    object AudioChannels {
        const val MONO: Int = 0
        const val STEREO: Int = 1
        const val SURROUND: Int = 2
    }

    @JvmStatic
    external fun setAudioDeviceChannels(channels: Int, tv: Boolean)

    @JvmStatic
    external fun getAudioDeviceChannels(tv: Boolean): Int

    const val AUDIO_MIN_VOLUME: Int = 0
    const val AUDIO_MAX_VOLUME: Int = 100

    @JvmStatic
    external fun setAudioDeviceVolume(volume: Int, tv: Boolean)

    @JvmStatic
    external fun getAudioDeviceVolume(tv: Boolean): Int

    const val AUDIO_LATENCY_MS_MAX: Int = 276

    @JvmStatic
    external fun getAudioLatency(): Int

    @JvmStatic
    external fun setAudioLatency(value: Int)

    // Audio/video sync: continuously trims queued audio to correct clock drift instead of one
    // large drop when the buffer limit is hit. Off by default.
    @JvmStatic
    external fun getAudioVideoSync(): Boolean

    @JvmStatic
    external fun setAudioVideoSync(value: Boolean)

    object OverlayScreenPosition {
        const val DISABLED: Int = 0
        const val TOP_LEFT: Int = 1
        const val TOP_CENTER: Int = 2
        const val TOP_RIGHT: Int = 3
        const val BOTTOM_LEFT: Int = 4
        const val BOTTOM_CENTER: Int = 5
        const val BOTTOM_RIGHT: Int = 6
    }

    @JvmStatic
    external fun getOverlayPosition(): Int

    @JvmStatic
    external fun setOverlayPosition(value: Int)

    const val OVERLAY_TEXT_SCALE_MIN: Int = 50
    const val OVERLAY_TEXT_SCALE_MAX: Int = 300

    @JvmStatic
    external fun getOverlayTextScalePercentage(): Int

    @JvmStatic
    external fun setOverlayTextScalePercentage(value: Int)

    @JvmStatic
    external fun isOverlayFPSEnabled(): Boolean

    @JvmStatic
    external fun setOverlayFPSEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayDrawCallsPerFrameEnabled(): Boolean

    @JvmStatic
    external fun setOverlayDrawCallsPerFrameEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayCPUUsageEnabled(): Boolean

    @JvmStatic
    external fun setOverlayCPUUsageEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayCPUPerCoreUsageEnabled(): Boolean

    @JvmStatic
    external fun setOverlayCPUPerCoreUsageEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayRAMUsageEnabled(): Boolean

    @JvmStatic
    external fun setOverlayRAMUsageEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayVRAMUsageEnabled(): Boolean

    @JvmStatic
    external fun setOverlayVRAMUsageEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayDebugEnabled(): Boolean

    @JvmStatic
    external fun setOverlayDebugEnabled(value: Boolean)

    // Quality of Life: same 5-second-average FPS as isOverlayFPSEnabled, shown as ms/frame instead.
    @JvmStatic
    external fun isOverlayFrameTimeEnabled(): Boolean

    @JvmStatic
    external fun setOverlayFrameTimeEnabled(value: Boolean)

    // Quality of Life: coarse Android thermal level (Nominal..Shutdown), not a precise
    // temperature - see NativeEmulation.updateThermalStatus for where the live value comes from.
    // Only ever shows anything on Android 10 (API 29) and up; harmless no-op below that.
    @JvmStatic
    external fun isOverlayDeviceThermalStatusEnabled(): Boolean

    @JvmStatic
    external fun setOverlayDeviceThermalStatusEnabled(value: Boolean)

    // Quality of Life: lays every enabled stat line above out side by side instead of stacked.
    @JvmStatic
    external fun isOverlayHorizontalLayoutEnabled(): Boolean

    @JvmStatic
    external fun setOverlayHorizontalLayoutEnabled(value: Boolean)

    // Quality of Life: HUD/notification background opacity, 0-100%. Stored as 0.0-1.0 natively.
    @JvmStatic
    external fun getOverlayBackgroundOpacityPercentage(): Int

    @JvmStatic
    external fun setOverlayBackgroundOpacityPercentage(opacityPercentage: Int)

    @JvmStatic
    external fun getNotificationsPosition(): Int

    @JvmStatic
    external fun setNotificationsPosition(value: Int)

    @JvmStatic
    external fun getNotificationsTextScalePercentage(): Int

    @JvmStatic
    external fun setNotificationsTextScalePercentage(value: Int)

    @JvmStatic
    external fun isNotificationControllerProfilesEnabled(): Boolean

    @JvmStatic
    external fun setNotificationControllerProfilesEnabled(value: Boolean)

    @JvmStatic
    external fun isNotificationShaderCompilerEnabled(): Boolean

    @JvmStatic
    external fun setNotificationShaderCompilerEnabled(value: Boolean)

    @JvmStatic
    external fun isNotificationFriendListEnabled(): Boolean

    @JvmStatic
    external fun setNotificationFriendListEnabled(value: Boolean)

    object ConsoleLanguage {
        const val JAPANESE: Int = 0
        const val ENGLISH: Int = 1
        const val FRENCH: Int = 2
        const val GERMAN: Int = 3
        const val ITALIAN: Int = 4
        const val SPANISH: Int = 5
        const val CHINESE: Int = 6
        const val KOREAN: Int = 7
        const val DUTCH: Int = 8
        const val PORTUGUESE: Int = 9
        const val RUSSIAN: Int = 10
        const val TAIWANESE: Int = 11
    }

    @JvmStatic
    external fun getConsoleLanguage(): Int

    @JvmStatic
    external fun setConsoleLanguage(value: Int)

    /**
     * @return the selected driver directory path. If it's null, then the default system driver will be used.
     */
    @JvmStatic
    external fun getCustomDriverPath(): String?

    /**
     * Sets the selected driver directory [path]. To use the default system driver, pass a null [path].
     */
    @JvmStatic
    external fun setCustomDriverPath(path: String?)

    object NetworkService {
        const val OFFLINE = 0
        const val NINTENDO = 1
        const val PRETENDO = 2
        const val CUSTOM = 3
    }

    @JvmStatic
    external fun getAccountNetworkService(persistentId: Int): Int


    @JvmStatic
    external fun setAccountNetworkService(persistentId: Int, networkService: Int)


    @JvmStatic
    external fun getAccountPersistentId(): Int

    @JvmStatic
    external fun setAccountPersistentId(persistentId: Int)

    @JvmStatic
    external fun isEmulateSkylanderPortalEnabled(): Boolean

    @JvmStatic
    external fun setEmulateSkylanderPortalEnabled(enabled: Boolean)

    @JvmStatic
    external fun isEmulateInfinityBaseEnabled(): Boolean

    @JvmStatic
    external fun setEmulateInfinityBaseEnabled(enabled: Boolean)

    @JvmStatic
    external fun isEmulateDimensionsToypadEnabled(): Boolean

    @JvmStatic
    external fun setEmulateDimensionsToypadEnabled(enabled: Boolean)

    // Deep IR Optimizations: dead code elimination fixpoint loop + redundant IR segment
    // merging in the PPC recompiler. Off by default.
    @JvmStatic
    external fun isAdvancedIMLOptimizationsEnabled(): Boolean

    @JvmStatic
    external fun setAdvancedIMLOptimizationsEnabled(enabled: Boolean)

    // Fast Audio Resampling: forces every AX voice to nearest-neighbor sample fetching
    // instead of whatever mode the game requested (Tap/Linear).
    @JvmStatic
    external fun isFastAudioResamplingEnabled(): Boolean

    @JvmStatic
    external fun setFastAudioResamplingEnabled(enabled: Boolean)

    // Force Audio Downmix: when TV output is Stereo/Mono, mixes Center/LFE/Surround into the
    // front L/R pair instead of dropping them.
    @JvmStatic
    external fun isForceAudioDownmixEnabled(): Boolean

    @JvmStatic
    external fun setForceAudioDownmixEnabled(enabled: Boolean)

    // Limitar o processamento de áudio assíncrono: while the audio buffer is critically low,
    // polls the backend more aggressively to try to refill it before it runs dry.
    @JvmStatic
    external fun isAudioBufferSyncEnabled(): Boolean

    @JvmStatic
    external fun setAudioBufferSyncEnabled(enabled: Boolean)

    object MemoryCeiling {
        const val GB_2: Int = 2048
        const val GB_4: Int = 4096
        const val GB_6: Int = 6144
        const val GB_8: Int = 8192
        const val GB_12: Int = 12288
    }

    // Memory Usage Brake: while enabled, once this process's own RSS crosses the chosen ceiling
    // (MemoryCeiling above), immediately reclaims textures already considered safe to discard.
    @JvmStatic
    external fun isMemoryUsageBrakeEnabled(): Boolean

    @JvmStatic
    external fun setMemoryUsageBrakeEnabled(enabled: Boolean)

    @JvmStatic
    external fun getMemoryCeilingMB(): Int

    @JvmStatic
    external fun setMemoryCeilingMB(ceilingMB: Int)

    @JvmStatic
    external fun hasCustomNetworkConfiguration(): Boolean

    // Internal (emulated Wii U) clock. When true (default), the emulated console reads the
    // device's real clock, same as always. When false, getRtcCustomTimestamp/setRtcCustomTimestamp
    // (Unix epoch seconds, UTC) is used instead - only takes effect on the next launched title.
    @JvmStatic
    external fun getRtcUseSystemTime(): Boolean

    @JvmStatic
    external fun setRtcUseSystemTime(useSystemTime: Boolean)

    @JvmStatic
    external fun getRtcCustomTimestamp(): Long

    @JvmStatic
    external fun setRtcCustomTimestamp(timestamp: Long)

    @JvmStatic
    external fun isQuickControllerProfileSwitcherEnabled(): Boolean

    @JvmStatic
    external fun setQuickControllerProfileSwitcherEnabled(enabled: Boolean)

    // ─── Limit to 30 FPS / Emulation speed (global default) ────────────
    @JvmStatic
    external fun getLimitToHalfFps(): Boolean

    @JvmStatic
    external fun setLimitToHalfFps(value: Boolean)

    /** 100-200, e.g. 150 = 1.5x. */
    @JvmStatic
    external fun getEmulationSpeedPercent(): Int

    @JvmStatic
    external fun setEmulationSpeedPercent(value: Int)

    // V11.45: "Full sync at GX2DrawDone" (config gx2drawdone_sync, padrao ligado)
    @JvmStatic
    external fun getGX2DrawDoneSync(): Boolean

    @JvmStatic
    external fun setGX2DrawDoneSync(value: Boolean)

    // V11.46: pular desenhos so de profundidade (sombras) no OpenGL
    @JvmStatic
    external fun getSkipDepthOnlyDraws(): Boolean

    @JvmStatic
    external fun setSkipDepthOnlyDraws(value: Boolean)
}
