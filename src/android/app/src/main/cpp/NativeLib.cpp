#include "JNIUtils.h"
#include "AndroidMemoryManager.h"

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, [[maybe_unused]] void* reserved)
{
	JNIUtils::SetJavaVM(vm);
	// Starts the background RAM monitor thread right away, before settings.xml even exists to be
	// read. Its "Memory Usage Brake" ceiling starts at 0 (disabled) and is pushed in from the
	// user's saved config once it's actually loaded - see initializeEmulation() in
	// NativeEmulation.cpp. The system-wide low/critical memory tracking (unrelated to that brake)
	// is active immediately.
	AndroidMemoryManager::Init();
	return JNI_VERSION_1_6;
}
