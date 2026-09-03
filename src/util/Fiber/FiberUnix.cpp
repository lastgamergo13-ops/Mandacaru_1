#include "Fiber.h"
#include <atomic>
#include <cstdlib>
#include "Cemu/Logging/CemuLogging.h"

#ifdef __ANDROID__
// Bionic (a libc do Android) nunca implementou ucontext.h (getcontext/makecontext/
// swapcontext) - e uma limitacao conhecida e antiga do NDK. Por isso usamos
// Boost.Context (biblioteca ja usada em outras partes do projeto) como
// implementacao alternativa de troca de stack no Android.
#include <boost/context/detail/fcontext.hpp>

using namespace boost::context::detail;

thread_local Fiber* sCurrentFiber{};

namespace
{
	void FiberTrampoline(transfer_t t)
	{
		// So executado na primeira vez que se troca para esse fiber.
		// 't.data' e um ponteiro para o Fiber que fez a troca para nos (ver
		// Switch() abaixo) - atualiza o slot de contexto DELE com o continuation
		// que o Boost.Context acabou de criar pra ele, exatamente como toda
		// retomada normal faz. sCurrentFiber ja aponta pra nos mesmos (Switch()
		// seta isso antes do jump), entao usamos ele pra saber "quem somos".
		Fiber* switchedFrom = static_cast<Fiber*>(t.data);
		*static_cast<fcontext_t*>(switchedFrom->AndroidGetContextSlot()) = t.fctx;

		Fiber* self = sCurrentFiber;
		self->AndroidRunEntryPoint();
		// FiberEntryPoint nao deveria retornar (mesma premissa do resto do projeto
		// pras outras plataformas). Se retornar, nao ha um destino valido pra saltar.
		std::abort();
	}
} // namespace

Fiber::Fiber(void(*FiberEntryPoint)(void* userParam), void* userParam, void* privateData) : m_privateData(privateData)
{
	if (!FiberEntryPoint)
		cemuLog_log(LogType::Force, "Fiber: constructed with a null FiberEntryPoint (userParam={})", userParam);

	const size_t stackSize = 2 * 1024 * 1024;
	m_stackPtr = malloc(stackSize);
	if (!m_stackPtr)
		cemuLog_log(LogType::Force, "Fiber: malloc({}) for the fiber stack failed", stackSize);

	// Boost.Context espera o ponteiro para o topo (endereco mais alto) da stack.
	void* stackTop = static_cast<char*>(m_stackPtr) + stackSize;
	fcontext_t ctx = make_fcontext(stackTop, stackSize, &FiberTrampoline);
	if (!ctx)
		cemuLog_log(LogType::Force, "Fiber: make_fcontext() returned null (stackPtr={}, stackTop={}, size={}) - Boost.Context may be broken for this build/ABI", m_stackPtr, stackTop, stackSize);

	m_implData = new fcontext_t(ctx);
	m_androidEntryPoint = FiberEntryPoint;
	m_androidUserParam = userParam;
}

Fiber::Fiber(void* privateData) : m_privateData(privateData)
{
	// Fiber representando a thread atual (ex: a thread principal). O contexto
	// so passa a ser valido depois que essa fiber ceder o controle pela primeira
	// vez via Switch() - antes disso nunca sera usado como alvo de um jump_fcontext.
	m_implData = new fcontext_t();
	m_stackPtr = nullptr;
	m_androidEntryPoint = nullptr;
	m_androidUserParam = nullptr;
}

Fiber::~Fiber()
{
	if (m_stackPtr)
		free(m_stackPtr);
	delete static_cast<fcontext_t*>(m_implData);
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

	fcontext_t* targetCtx = static_cast<fcontext_t*>(targetFiber.m_implData);

	if (!targetCtx || !*targetCtx)
	{
		// this would otherwise crash inside jump_fcontext() with pc=0 and no useful backtrace.
		// logging it here first pinpoints it precisely to a bad/uninitialized fcontext_t instead.
		cemuLog_log(LogType::Force, "Fiber::Switch: about to jump into a null/invalid fcontext_t (target entryPoint={}, userParam={}, stackPtr={}) - this will crash",
			(void*)targetFiber.m_androidEntryPoint, targetFiber.m_androidUserParam, targetFiber.m_stackPtr);
	}

	// Passa 'leavingFiber' (nos mesmos) como data - assim, quando alguem trocar
	// de volta pra ca (nesse mesmo ponto do codigo, mais tarde), sabemos - via
	// result.data - qual fiber acabou de entrar e cujo continuation precisa ser
	// atualizado. Nunca atualizamos nosso proprio slot aqui.
	transfer_t result = jump_fcontext(*targetCtx, leavingFiber);

	Fiber* switchedFrom = static_cast<Fiber*>(result.data);
	*static_cast<fcontext_t*>(switchedFrom->m_implData) = result.fctx;

	std::atomic_thread_fence(std::memory_order_seq_cst);
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
