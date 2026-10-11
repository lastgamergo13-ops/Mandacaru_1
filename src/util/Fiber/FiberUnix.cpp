#include "Fiber.h"
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
#include "Cemu/Logging/CemuLogging.h"

#ifdef __ANDROID__
// Bionic (a libc do Android) nunca implementou ucontext.h (getcontext/makecontext/
// swapcontext) - e uma limitacao conhecida e antiga do NDK. Uma primeira tentativa
// usou Boost.Context (troca de stack manual via assembly) como alternativa, mas
// apos uma extensa investigacao de crashes intermitentes (registradores dedicados
// do JIT - x27/x28 - perdendo o valor correto de forma esporadica, sem causa raiz
// isolavel apesar de varias correcoes reais em codigo ao redor), trocamos pra uma
// abordagem mais pesada porem muito mais confiavel: cada "fiber" agora e uma
// THREAD REAL do sistema operacional (pthread), e a troca entre fibers e feita
// deixando so uma thread por vez acordada, via um semaforo dedicado por fiber.
// Isso delega a preservacao de registradores inteiramente ao escalonador/kernel
// do Linux (via troca de contexto real de thread), que e uma garantia muito mais
// testada/confiavel nesse quesito do que uma biblioteca de troca de stack manual.
#include <pthread.h>
#include <semaphore.h>

thread_local Fiber* sCurrentFiber{};

namespace
{
	// Estado interno de cada fiber Android. Alocado no heap e guardado (como
	// void*) em Fiber::m_implData, acessado via AndroidGetContextSlot().
	struct AndroidFiberImpl
	{
		sem_t resumeSem{};
		pthread_t thread{};
		bool hasOwnThread = false;
		// Setado pelo destrutor do Fiber. A pthread dona do fiber (parada em sem_wait) ao acordar
		// com isso setado NAO retoma o codigo do jogo: libera o impl e termina sozinha.
		// Sem isso, o destrutor liberava o impl/semaforo enquanto a pthread ainda estava (ou ia
		// estar) dentro de Fiber::Switch() -> use-after-free: a thread de uma OSThread encerrada
		// (OSExitThread) "acordava" com o semaforo destruido e continuava executando com
		// ponteiros liberados (crash em __OSThreadLoadContext logo depois do OSExitThread).
		std::atomic<bool> dying{ false };
	};

	// sem_wait pode retornar com EINTR (sinais - ART, perf, profiler). Sem repetir, o fiber
	// acordava sem ninguem ter feito switch para ele.
	inline void AndroidSemWait(sem_t* sem)
	{
		while (sem_wait(sem) != 0 && errno == EINTR)
		{
		}
	}

	// Ponto de entrada da pthread real por tras de cada fiber "criado" (nao o
	// que envolve a thread atual - esse nunca passa por aqui). Fica bloqueada
	// esperando sua vez ANTES de rodar qualquer coisa do FiberEntryPoint real,
	// exatamente como um fiber recem-criado via ucontext/Boost.Context so
	// comecava a executar no primeiro Switch() pra ele, nunca antes.
	void* AndroidFiberThreadEntry(void* param)
	{
		Fiber* self = static_cast<Fiber*>(param);
		auto* impl = static_cast<AndroidFiberImpl*>(self->AndroidGetContextSlot());

		AndroidSemWait(&impl->resumeSem);
		if (impl->dying.load(std::memory_order_acquire))
		{
			// fiber destruido antes de ter rodado alguma vez
			sem_destroy(&impl->resumeSem);
			delete impl;
			return nullptr;
		}
		sCurrentFiber = self;
		self->AndroidRunEntryPoint();
		// FiberEntryPoint nao deveria retornar (mesma premissa do resto do
		// projeto pras outras plataformas). Se retornar, nao ha destino valido.
		std::abort();
		return nullptr;
	}
} // namespace

Fiber::Fiber(void(*FiberEntryPoint)(void* userParam), void* userParam, void* privateData) : m_privateData(privateData)
{
	if (!FiberEntryPoint)
		cemuLog_log(LogType::Force, "Fiber: constructed with a null FiberEntryPoint (userParam={})", userParam);

	auto* impl = new AndroidFiberImpl();
	sem_init(&impl->resumeSem, 0, 0);
	m_implData = impl;
	m_androidEntryPoint = FiberEntryPoint;
	m_androidUserParam = userParam;
	m_stackPtr = nullptr; // a pthread gerencia a propria pilha internamente

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	// Mesmo tamanho de pilha que a implementacao anterior usava (o padrao do
	// pthread no Android costuma ser menor, 1MB) - preserva a mesma margem.
	pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);

	int rc = pthread_create(&impl->thread, &attr, &AndroidFiberThreadEntry, this);
	pthread_attr_destroy(&attr);
	if (rc != 0)
	{
		cemuLog_log(LogType::Force, "Fiber: pthread_create() failed, rc={}", rc);
	}
	else
	{
		impl->hasOwnThread = true;
		// Ninguem precisa dar join nessas threads (elas vivem pelo tempo de
		// vida do fiber, que por sua vez normalmente vive ate o processo
		// terminar - o mesmo valia pras stacks da implementacao anterior,
		// que tambem nunca eram "encerradas" de forma limpa).
		pthread_detach(impl->thread);
	}
}

Fiber::Fiber(void* privateData) : m_privateData(privateData)
{
	// Fiber representando a thread atual (ex: a thread principal, ou a propria
	// thread real de um core ja rodando). Nao cria pthread nova - so precisa
	// do proprio semaforo, usado quando ESSA fiber for trocada de volta pra
	// depois de ceder o controle pela primeira vez via Switch().
	auto* impl = new AndroidFiberImpl();
	sem_init(&impl->resumeSem, 0, 0);
	m_implData = impl;
	m_stackPtr = nullptr;
	m_androidEntryPoint = nullptr;
	m_androidUserParam = nullptr;
}

Fiber::~Fiber()
{
	auto* impl = static_cast<AndroidFiberImpl*>(m_implData);
	m_implData = nullptr;
	if (!impl)
		return;
	if (impl->hasOwnThread)
	{
		// A pthread do fiber esta parada (ou prestes a parar) em sem_wait. NAO liberar o impl
		// aqui: sinaliza e deixa a propria thread liberar e terminar (ver Switch() e
		// AndroidFiberThreadEntry). Depois do sem_post nao tocamos mais no impl.
		impl->dying.store(true, std::memory_order_release);
		sem_post(&impl->resumeSem);
	}
	else
	{
		// fiber que representa uma thread existente (sem pthread propria)
		sem_destroy(&impl->resumeSem);
		delete impl;
	}
}

Fiber* Fiber::PrepareCurrentThread(void* privateData)
{
	cemu_assert_debug(sCurrentFiber == nullptr);
	sCurrentFiber = new Fiber(privateData);
	return sCurrentFiber;
}

void Fiber::Switch(Fiber& targetFiber)
{
	// Cada fiber agora E uma thread real, entao "sCurrentFiber" (thread_local)
	// e fixo pra sempre em cada thread do SO, setado uma unica vez (aqui em
	// baixo, ou em AndroidFiberThreadEntry/PrepareCurrentThread) - nao precisa
	// ser reatribuido a cada troca, ao contrario da versao anterior baseada em
	// Boost.Context (onde uma unica thread real "era" varios fibers diferentes
	// ao longo do tempo).
	Fiber* leavingFiber = sCurrentFiber;
	auto* leavingImpl = static_cast<AndroidFiberImpl*>(leavingFiber->AndroidGetContextSlot());
	auto* targetImpl = static_cast<AndroidFiberImpl*>(targetFiber.AndroidGetContextSlot());

	// Acorda a thread real por tras do fiber alvo...
	sem_post(&targetImpl->resumeSem);
	// ...e dorme na propria, esperando alguem trocar de volta pra ca.
	AndroidSemWait(&leavingImpl->resumeSem);
	if (leavingImpl->dying.load(std::memory_order_acquire))
	{
		// Este fiber foi destruido (ex.: OSThread encerrada e desalocada). Nunca mais volta ao
		// codigo do jogo: libera o proprio impl e encerra a pthread.
		sem_destroy(&leavingImpl->resumeSem);
		delete leavingImpl;
		pthread_exit(nullptr);
	}
}

void* Fiber::GetFiberPrivateData()
{
	return sCurrentFiber->m_privateData;
}

void Fiber::AndroidRunEntryPoint()
{
	m_androidEntryPoint(m_androidUserParam);
}

#else // !__ANDROID__

#include <ucontext.h>

thread_local Fiber* sCurrentFiber{};

Fiber::Fiber(void(*FiberEntryPoint)(void* userParam), void* userParam, void* privateData) : m_privateData(privateData)
{
	ucontext_t* ctx = (ucontext_t*)malloc(sizeof(ucontext_t));
	
	const size_t stackSize = 2 * 1024 * 1024;
	m_stackPtr = malloc(stackSize);

	getcontext(ctx);
	ctx->uc_stack.ss_sp = m_stackPtr;
	ctx->uc_stack.ss_size = stackSize;
	ctx->uc_link = &ctx[0];
#ifdef __arm64__
	// https://www.man7.org/linux/man-pages/man3/makecontext.3.html#NOTES
	makecontext(ctx, (void(*)())FiberEntryPoint, 2, (uint64) userParam >> 32, userParam);
#else
	makecontext(ctx, (void(*)())FiberEntryPoint, 1, userParam);
#endif
	this->m_implData = (void*)ctx;
}

Fiber::Fiber(void* privateData) : m_privateData(privateData)
{
	ucontext_t* ctx = (ucontext_t*)malloc(sizeof(ucontext_t));
	getcontext(ctx);
	this->m_implData = (void*)ctx;
	m_stackPtr = nullptr;
}

Fiber::~Fiber()
{
	if(m_stackPtr)
		free(m_stackPtr);
	free(m_implData);
}

Fiber* Fiber::PrepareCurrentThread(void* privateData)
{
	cemu_assert_debug(sCurrentFiber == nullptr);
    sCurrentFiber = new Fiber(privateData);
	return sCurrentFiber;
}

void Fiber::Switch(Fiber& targetFiber)
{
    Fiber* leavingFiber = sCurrentFiber;
    sCurrentFiber = &targetFiber;
	std::atomic_thread_fence(std::memory_order_seq_cst);
	swapcontext((ucontext_t*)(leavingFiber->m_implData), (ucontext_t*)(targetFiber.m_implData));
	std::atomic_thread_fence(std::memory_order_seq_cst);
}

void* Fiber::GetFiberPrivateData()
{
	return sCurrentFiber->m_privateData;
}

#endif // __ANDROID__
