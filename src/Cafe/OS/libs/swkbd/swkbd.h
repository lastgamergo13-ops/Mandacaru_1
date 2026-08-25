#include "Cafe/OS/RPL/COSModule.h"

void swkbd_render(bool mainWindow);
bool swkbd_hasKeyboardInputHook();
void swkbd_keyInput(uint32 keyCode);

namespace swkbd
{
	COSModule* GetModule();

	// Camada de callbacks usada por plataformas (Android) que mostram um teclado
	// nativo do proprio sistema, em vez de desenhar um teclado customizado via
	// ImGui como o desktop faz (ver swkbd_render). O estado do texto continua
	// sendo gerenciado internamente (swkbdInternalState); essas callbacks so
	// avisam a UI nativa quando o teclado deve aparecer/desaparecer.
	class swkbdCallbacks
	{
	  public:
		virtual ~swkbdCallbacks() = default;
		virtual void showSoftwareKeyboard(const std::string& initialText, sint32 maxLength) = 0;
		virtual void hideSoftwareKeyboard() = 0;
	};

	void setSwkbdCallbacks(std::shared_ptr<swkbdCallbacks> callbacks);

	// Injeta um caractere/tecla de controle no buffer de texto atual do teclado
	// emulado. Usa o mesmo caminho que o teclado desenhado no desktop usa.
	void keyInput(uint32 keyCode);

	constexpr uint32 BACKSPACE_KEYCODE = 8;
	constexpr uint32 RETURN_KEYCODE = 13;
} // namespace swkbd
