#pragma once
// Frame Generation (Optical Flow + extrapolacao v8) para o renderer OpenGL ES do Android.
// Usa compute shaders GLSL ES pre-convertidos (assets/resources/ffx-frame-generation/shaders_glsl).
// Todas as funcoes devem ser chamadas na thread do Latte, com o contexto GL atual (dentro de OpenGLRenderer::SwapBuffers).
namespace FrameGenGL
{
	// 1) ANTES do swap real: o backbuffer contem o frame real. Copia para a textura "atual".
	void CaptureRealFrame();
	// 2) DEPOIS do swap real: gera e apresenta os frames extras. presentMain() deve fazer o swap da janela principal.
	void GenerateAfterRealSwap(void (*presentMain)());
	// libera os recursos GL (contexto atual)
	void Shutdown();
}
