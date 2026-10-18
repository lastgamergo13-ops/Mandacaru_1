#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "WindowSystem.h"
#include "Cafe/HW/Latte/Core/PerfLog.h"
#include "config/CemuConfig.h"
#include "Cemu/Logging/CemuLogging.h"
#include "Cafe/HW/Latte/Core/RendererInfo.h"

performanceMonitor_t performanceMonitor{};

void LattePerformanceMonitor_frameEnd()
{
	// per-frame stats
	performanceMonitor.gpuTime_shaderCreate.frameFinished();
	performanceMonitor.gpuTime_frameTime.frameFinished();
	performanceMonitor.gpuTime_idleTime.frameFinished();
	performanceMonitor.gpuTime_fenceTime.frameFinished();

	performanceMonitor.gpuTime_dcStageTextures.frameFinished();
	performanceMonitor.gpuTime_dcStageVertexMgr.frameFinished();
	performanceMonitor.gpuTime_dcStageShaderAndUniformMgr.frameFinished();
	performanceMonitor.gpuTime_dcStageIndexMgr.frameFinished();
	performanceMonitor.gpuTime_dcStageMRT.frameFinished();
	performanceMonitor.gpuTime_dcStageDrawcallAPI.frameFinished();
	performanceMonitor.gpuTime_waitForAsync.frameFinished();

	uint32 elapsedTime = GetTickCount() - performanceMonitor.cycle[performanceMonitor.cycleIndex].lastUpdate;
	if (elapsedTime >= 1000)
	{
		bool isFirstUpdate = performanceMonitor.cycle[performanceMonitor.cycleIndex].lastUpdate == 0;
		// sum up raw stats
		uint32 totalElapsedTime = GetTickCount() - performanceMonitor.cycle[(performanceMonitor.cycleIndex + 1) % PERFORMANCE_MONITOR_TRACK_CYCLES].lastUpdate;
		uint32 totalElapsedTimeFPS = GetTickCount() - performanceMonitor.cycle[(performanceMonitor.cycleIndex + PERFORMANCE_MONITOR_TRACK_CYCLES - 1) % PERFORMANCE_MONITOR_TRACK_CYCLES].lastUpdate;
		uint32 elapsedFrames = 0;
		uint32 elapsedFrames2S = 0; // elapsed frames for last two entries (seconds)
		uint64 skippedCycles = 0;
		uint64 vertexDataUploaded = 0;
		uint64 vertexDataCached = 0;
		uint64 uniformBankUploadedData = 0;
		uint64 uniformBankUploadedCount = 0;
		uint64 indexDataUploaded = 0;
		uint64 indexDataCached = 0;
		uint32 frameCounter = 0;
		uint32 drawCallCounter = 0;
		uint32 fastDrawCallCounter = 0;
		uint32 shaderBindCounter = 0;
		uint32 recompilerLeaveCount = 0;
		uint32 threadLeaveCount = 0;
		for (sint32 i = 0; i < PERFORMANCE_MONITOR_TRACK_CYCLES; i++)
		{
			elapsedFrames += performanceMonitor.cycle[i].frameCounter;
			skippedCycles += performanceMonitor.cycle[i].skippedCycles;
			vertexDataUploaded += performanceMonitor.cycle[i].vertexDataUploaded;
			vertexDataCached += performanceMonitor.cycle[i].vertexDataCached;
			uniformBankUploadedData += performanceMonitor.cycle[i].uniformBankUploadedData;
			uniformBankUploadedCount += performanceMonitor.cycle[i].uniformBankUploadedCount;
			indexDataUploaded += performanceMonitor.cycle[i].indexDataUploaded;
			indexDataCached += performanceMonitor.cycle[i].indexDataCached;
			frameCounter += performanceMonitor.cycle[i].frameCounter;
			drawCallCounter += performanceMonitor.cycle[i].drawCallCounter;
			fastDrawCallCounter += performanceMonitor.cycle[i].fastDrawCallCounter;
			shaderBindCounter += performanceMonitor.cycle[i].shaderBindCount;
			recompilerLeaveCount += performanceMonitor.cycle[i].recompilerLeaveCount;
			threadLeaveCount += performanceMonitor.cycle[i].threadLeaveCount;
		}
		elapsedFrames = std::max<uint32>(elapsedFrames, 1);
		elapsedFrames2S = performanceMonitor.cycle[(performanceMonitor.cycleIndex + PERFORMANCE_MONITOR_TRACK_CYCLES - 0) % PERFORMANCE_MONITOR_TRACK_CYCLES].frameCounter;
		elapsedFrames2S += performanceMonitor.cycle[(performanceMonitor.cycleIndex + PERFORMANCE_MONITOR_TRACK_CYCLES - 1) % PERFORMANCE_MONITOR_TRACK_CYCLES].frameCounter;
		elapsedFrames2S = std::max<uint32>(elapsedFrames2S, 1);
		// calculate stats
		uint64 passedCycles = PPCInterpreter_getMainCoreCycleCounter() - performanceMonitor.cycle[(performanceMonitor.cycleIndex + 1) % PERFORMANCE_MONITOR_TRACK_CYCLES].lastCycleCount;
		passedCycles -= skippedCycles;
		uint64 vertexDataUploadPerFrame = (vertexDataUploaded / (uint64)elapsedFrames);
		vertexDataUploadPerFrame /= 1024ULL;
		uint64 vertexDataCachedPerFrame = (vertexDataCached / (uint64)elapsedFrames);
		vertexDataCachedPerFrame /= 1024ULL;
		uint64 uniformBankDataUploadedPerFrame = (uniformBankUploadedData / (uint64)elapsedFrames);
		uniformBankDataUploadedPerFrame /= 1024ULL;
		uint32 uniformBankCountUploadedPerFrame = (uint32)(uniformBankUploadedCount / (uint64)elapsedFrames);
		uint64 indexDataUploadPerFrame = (indexDataUploaded / (uint64)elapsedFrames);

		double fps = (double)elapsedFrames2S * 1000.0 / (double)totalElapsedTimeFPS;
		uint32 shaderBindsPerFrame = shaderBindCounter / elapsedFrames;
		passedCycles = passedCycles * 1000ULL / totalElapsedTime;
		uint32 rlps = (uint32)((uint64)recompilerLeaveCount * 1000ULL / (uint64)totalElapsedTime);
		uint32 tlps = (uint32)((uint64)threadLeaveCount * 1000ULL / (uint64)totalElapsedTime);
		// set stats
		performanceMonitor.stats.indexDataUploadPerFrame = indexDataUploadPerFrame;
		// next counter cycle
		sint32 nextCycleIndex = (performanceMonitor.cycleIndex + 1) % PERFORMANCE_MONITOR_TRACK_CYCLES;
		performanceMonitor.cycle[nextCycleIndex].drawCallCounter = 0;
		performanceMonitor.cycle[nextCycleIndex].fastDrawCallCounter = 0;
		performanceMonitor.cycle[nextCycleIndex].frameCounter = 0;
		performanceMonitor.cycle[nextCycleIndex].shaderBindCount = 0;
		performanceMonitor.cycle[nextCycleIndex].lastCycleCount = PPCInterpreter_getMainCoreCycleCounter();
		performanceMonitor.cycle[nextCycleIndex].skippedCycles = 0;
		performanceMonitor.cycle[nextCycleIndex].vertexDataUploaded = 0;
		performanceMonitor.cycle[nextCycleIndex].vertexDataCached = 0;
		performanceMonitor.cycle[nextCycleIndex].uniformBankUploadedData = 0;
		performanceMonitor.cycle[nextCycleIndex].uniformBankUploadedCount = 0;
		performanceMonitor.cycle[nextCycleIndex].indexDataUploaded = 0;
		performanceMonitor.cycle[nextCycleIndex].indexDataCached = 0;
		performanceMonitor.cycle[nextCycleIndex].recompilerLeaveCount = 0;
		performanceMonitor.cycle[nextCycleIndex].threadLeaveCount = 0;
		performanceMonitor.cycleIndex = nextCycleIndex;

		// next update in 1 second
		performanceMonitor.cycle[performanceMonitor.cycleIndex].lastUpdate = GetTickCount();

		// ---- [Perf] a cada ~5 s (ver PerfLog.h) ----
		{
			static uint32 s_perfTick = 0;
			static uint32 s_perfLastMs = 0;
			if (!isFirstUpdate && (++s_perfTick % 5) == 0)
			{
				const uint32 nowMs = GetTickCount();
				const double wall = s_perfLastMs ? (nowMs - s_perfLastMs) / 1000.0 : 0.0;
				s_perfLastMs = nowMs;
				const double emuMHz = (double)passedCycles / 1.0e6;
				auto& cfg = GetConfig();
				std::string api, drv, cust;
				RendererInfo::Get(api, drv, cust);
				cemuLog_log(LogType::Force, "[Perf] fps={:.1f} draws/frame={} shaderBinds/frame={} | velocidade emulada: {:.0f} MHz PPC = {:.0f}% do Espresso (1243 MHz; <100% = jogo em camera lenta) | saidas do recompilador/s={} trocas de thread/s={} | GPU(thread Latte) ultimo frame: total={}us ocioso(esperando CPU)={}us fence={}us asyncWait={}us | limit30fps={} velocidade%={} vsync={} api={} driver={}{}",
					fps, drawCallCounter / elapsedFrames, shaderBindsPerFrame, emuMHz, emuMHz * 100.0 / 1243.125, rlps, tlps,
					(uint64)PPCTimer_tscToMicroseconds(performanceMonitor.gpuTime_frameTime.getPreviousFrameValue()),
					(uint64)PPCTimer_tscToMicroseconds(performanceMonitor.gpuTime_idleTime.getPreviousFrameValue()),
					(uint64)PPCTimer_tscToMicroseconds(performanceMonitor.gpuTime_fenceTime.getPreviousFrameValue()),
					(uint64)PPCTimer_tscToMicroseconds(performanceMonitor.gpuTime_waitForAsync.getPreviousFrameValue()),
					(bool)cfg.limit_to_half_fps.GetValue(), (uint32)cfg.emulation_speed_percent.GetValue(), (int)cfg.vsync.GetValue(),
					api, drv, cust.empty() ? "" : (" (custom: " + cust + ")"));
				{
					auto us = [](LattePerfStatTimer& t) { return (uint64)PPCTimer_tscToMicroseconds(t.getPreviousFrameValue()); };
					cemuLog_log(LogType::Force, "[Perf] etapas do ultimo frame (us, so draws completos): texturas+render targets={} MRT={} shader+uniforms={} indices={} vertex={} chamada_de_draw_na_API={} esperando_async={}",
						us(performanceMonitor.gpuTime_dcStageTextures), us(performanceMonitor.gpuTime_dcStageMRT), us(performanceMonitor.gpuTime_dcStageShaderAndUniformMgr),
						us(performanceMonitor.gpuTime_dcStageIndexMgr), us(performanceMonitor.gpuTime_dcStageVertexMgr), us(performanceMonitor.gpuTime_dcStageDrawcallAPI), us(performanceMonitor.gpuTime_waitForAsync));
				}
				const std::string top = PerfLog::ThreadTop(wall);
				if (!top.empty())
					cemuLog_log(LogType::Force, "[Perf] CPU por thread (>=5%): {}", top);
			}
		}

		if (isFirstUpdate)
		{
			LatteOverlay_updateStats(0.0, 0, 0);
			WindowSystem::UpdateWindowTitles(false, false, 0.0);
		}
		else
		{
			LatteOverlay_updateStats(fps, drawCallCounter / elapsedFrames, fastDrawCallCounter / elapsedFrames);
			WindowSystem::UpdateWindowTitles(false, false, fps);
		}
	}
}

void LattePerformanceMonitor_frameBegin()
{
	performanceMonitor.vk.numDrawBarriersPerFrame.reset();
	performanceMonitor.vk.numBeginRenderpassPerFrame.reset();
}
