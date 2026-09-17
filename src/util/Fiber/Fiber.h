#pragma once

#if BOOST_OS_WINDOWS

#endif

class Fiber
{
public:
	Fiber(void(*FiberEntryPoint)(void* userParam), void* userParam, void* privateData);
	~Fiber();

	static Fiber* PrepareCurrentThread(void* privateData = nullptr);
	static void Switch(Fiber& targetFiber);
	static void* GetFiberPrivateData();

#ifdef __ANDROID__
	// Usado internamente pela troca de fiber baseada em pthread+semaforo no
	// Android - nao chame diretamente.
	void AndroidRunEntryPoint();
	// Ponteiro pro slot interno de implementacao (na verdade um AndroidFiberImpl*,
	// struct privada definida dentro de FiberUnix.cpp) - usado só pela troca de
	// fiber dentro de FiberUnix.cpp. Não mexa nisso fora dali.
	void* AndroidGetContextSlot() const { return m_implData; }
#endif

private:
	Fiber(void* privateData); // fiber from current thread

	void* m_implData{nullptr};
	void* m_privateData;
	void* m_stackPtr{ nullptr };
#ifdef __ANDROID__
	void (*m_androidEntryPoint)(void* userParam){nullptr};
	void* m_androidUserParam{nullptr};
#endif
};
