# TESTE: ON = fibers por corrotina no Android (usar com o perfil do jogo em nucleo unico). OFF = padrao (uma pthread por fiber).
set(MANDACARU_FCONTEXT_FIBERS ON)
add_library(CemuUtil
  boost/bluetooth.h
  bootSound/BootSoundReader.cpp
  bootSound/BootSoundReader.h
  ChunkedHeap/ChunkedHeap.h
  containers/flat_hash_map.hpp
  containers/IntervalBucketContainer.h
  containers/LookupTableL3.h
  containers/RangeStore.h
  containers/robin_hood.h
  containers/SmallBitset.h
  crypto/aes128.cpp
  crypto/aes128.h
  crypto/crc32.cpp
  crypto/crc32.h
  crypto/md5.cpp
  crypto/md5.h
  DXGIWrapper/DXGIWrapper.h
  EventService.h
  Fiber/Fiber.h
  helpers/ClassWrapper.h
  helpers/ConcurrentQueue.h
  helpers/enum_array.hpp
  helpers/fixedSizeList.h
  helpers/fspinlock.h
  helpers/helpers.cpp
  helpers/helpers.h
  helpers/MapAdaptor.h
  helpers/MemoryPool.h
  helpers/ringbuffer.h
  helpers/StateHasher.h
  helpers/Semaphore.h
  helpers/Serializer.cpp
  helpers/Serializer.h
  helpers/Singleton.h
  helpers/StringBuf.h
  helpers/StringHelpers.h
  helpers/StringParser.h
  helpers/SystemException.h
  helpers/TempState.h
  highresolutiontimer/HighResolutionTimer.cpp
  highresolutiontimer/HighResolutionTimer.h
  ImageWriter/bmp.h
  ImageWriter/tga.h
  IniParser/IniParser.cpp
  IniParser/IniParser.h
  math/glm.h
  math/quaternion.h
  math/vector2.h
  math/vector3.h
  MemMapper/MemMapper.h
  SystemInfo/SystemInfo.cpp
  SystemInfo/SystemInfo.h
  ThreadPool/ThreadPool.h
  tinyxml2/tinyxml2.cpp
  tinyxml2/tinyxml2.h
  VirtualHeap/VirtualHeap.cpp
  VirtualHeap/VirtualHeap.h
  Zir/Core/IR.cpp
  Zir/Core/IR.h
  Zir/Core/ZirUtility.h
  Zir/Core/ZpIRBuilder.h
  Zir/Core/ZpIRDebug.h
  Zir/Core/ZpIRPasses.h
  Zir/Core/ZpIRScheduler.h
  Zir/EmitterGLSL/ZpIREmitGLSL.cpp
  Zir/EmitterGLSL/ZpIREmitGLSL.h
  Zir/Passes/RegisterAllocatorForGLSL.cpp
  Zir/Passes/ZpIRRegisterAllocator.cpp
)

if(WIN32)
	target_sources(CemuUtil PRIVATE Fiber/FiberWin.cpp)
	target_sources(CemuUtil PRIVATE MemMapper/MemMapperWin.cpp)
	target_sources(CemuUtil PRIVATE SystemInfo/SystemInfoWin.cpp)
elseif(UNIX)
	if(ANDROID AND MANDACARU_FCONTEXT_FIBERS)
		# TESTE: fibers por corrotina (boost.context), todas na mesma thread do sistema (modelo do Cemu de PC)
		target_sources(CemuUtil PRIVATE Fiber/FiberFContext.cpp)
		target_compile_definitions(CemuUtil PUBLIC MANDACARU_FCONTEXT_FIBERS=1)
		target_link_libraries(CemuUtil PUBLIC Boost::context)
	else()
		target_sources(CemuUtil PRIVATE Fiber/FiberUnix.cpp)
	endif()
	target_sources(CemuUtil PRIVATE MemMapper/MemMapperUnix.cpp)
	target_sources(CemuUtil PRIVATE SystemInfo/SystemInfoUnix.cpp)
	if(NOT APPLE)
		target_sources(CemuUtil PRIVATE SystemInfo/SystemInfoLinux.cpp)
	else()
		target_sources(CemuUtil PRIVATE SystemInfo/SystemInfoMac.cpp)
	endif()
else()
	target_sources(CemuUtil PRIVATE SystemInfo/SystemInfoStub.cpp)
endif()

set_property(TARGET CemuUtil PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

cemu_use_precompiled_header(CemuUtil)

if (ENABLE_LIBUSB)
  target_sources(CemuUtil PRIVATE
    libusbWrapper/libusbWrapper.cpp
    libusbWrapper/libusbWrapper.h
  )
endif()

target_include_directories(CemuUtil PUBLIC "../")

target_link_libraries(CemuUtil PRIVATE
  CemuCommon
)
