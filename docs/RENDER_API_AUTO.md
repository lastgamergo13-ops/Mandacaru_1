# Render API: como escolher Vulkan ou OpenGL por jogo (modo Auto)

O Mandacaru tem 3 modos em **Configurações → Experimental → Render API**:

| Modo | O que faz |
|------|-----------|
| **Auto** | O emulador escolhe Vulkan ou OpenGL ES para cada jogo, na hora de entrar nele. Se a API escolhida falhar ao iniciar, tenta a outra. |
| **Vulkan** | Só usa Vulkan. Se não iniciar, o jogo dá erro (não cai para OpenGL). |
| **OpenGL** | Só usa OpenGL ES. Se não iniciar, o jogo dá erro (não cai para Vulkan). |

As duas formas abaixo **só valem com o modo Auto**. Nos modos Vulkan e OpenGL fixos, a API escolhida nas configurações sempre vence.

## Como o Auto decide (nesta ordem)

1. O **perfil do jogo** tem `graphics_api` definido → usa o que o perfil manda.
2. O Title ID do jogo está na **lista de preferência OpenGL** (`kAutoPreferOpenGLTitleIds`) → usa OpenGL.
3. O aparelho **não tem Vulkan** → usa OpenGL.
4. Nenhum dos anteriores → usa **Vulkan** (o backend mais testado).

A lista de preferência começa **vazia** de propósito: não existem dados medidos de compatibilidade por jogo, e inventar entradas seria chute. Enquanto ela estiver vazia e nenhum perfil definir `graphics_api`, o Auto usa Vulkan e só muda para OpenGL quando o Vulkan falha.

## Antes de tudo: descobrir o Title ID do jogo

O log do emulador mostra uma linha assim ao iniciar o jogo:

```
TitleId: 00050000-10101a00
```

Junte as duas metades para formar os 16 dígitos: `0005000010101a00`.

## Forma 1: perfil do jogo (Vulkan ou OpenGL)

Funciona para os dois lados e **tem prioridade sobre a lista**.

1. Abra a pasta de dados do app. No aparelho ela fica em:
   `Android/data/info.mandacarucemu.cemu/files/`
   (na versão debug o app usa `info.mandacarucemu.cemu.debug`).
2. Dentro dela, abra (ou crie) a pasta `gameProfiles/`.
3. Crie o arquivo `<TitleID com 16 dígitos>.ini`, por exemplo `0005000010101a00.ini`.
4. Coloque este conteúdo:

```ini
[Graphics]
graphics_api = 0
```

| Valor | API |
|-------|-----|
| `graphics_api = 0` | OpenGL ES |
| `graphics_api = 1` | Vulkan |

**Atenção:** o perfil do usuário **substitui** o perfil padrão do jogo, ele não é somado. Se o jogo já tiver um perfil padrão em `files/data/gameProfiles/default/<TitleID>.ini`, **copie esse arquivo** para `files/gameProfiles/` e só acrescente a seção `[Graphics]` (ou a linha `graphics_api` dentro dela, se a seção já existir). Senão o jogo perde as configurações que o perfil padrão trazia.

## Forma 2: lista de preferência no código (somente OpenGL)

Serve para deixar um jogo em OpenGL por padrão para todo mundo que usar o seu build. Para Vulkan não precisa de entrada, porque o padrão do Auto já é Vulkan.

1. Abra `src/android/app/src/main/cpp/NativeEmulation.cpp`.
2. Procure `kAutoPreferOpenGLTitleIds`.
3. Acrescente o Title ID como número hexadecimal de 64 bits, terminado em `ull`, um por linha:

```cpp
inline const std::vector<TitleId> kAutoPreferOpenGLTitleIds = {
	0x0005000010101a00ull, // Nome do jogo
	0x000500001010XXXXull, // Outro jogo
};
```

4. Faça commit, push e um novo build.

## Como conferir se funcionou

No log do emulador, ao entrar no jogo, aparece uma linha do Auto dizendo o motivo da escolha:

- `Render API (Auto): OpenGL ES (game profile)`
- `Render API (Auto): Vulkan (game profile)`
- `Render API (Auto): OpenGL ES (title ... na lista de preferencia)`
- `Render API (Auto): OpenGL ES (Vulkan indisponivel neste aparelho)`
- `Render API (Auto): Vulkan`

Se a API escolhida falhar ao iniciar, aparece também `tentando a outra API`.

## Observações

- A mudança vale na próxima vez que você **iniciar o jogo**, não durante a partida.
- A tela de edição de perfil do app **não tem** opção de API gráfica. Por isso o `graphics_api` é editado direto no arquivo `.ini`.
- Alguns jogos dependem de recursos que GPUs Mali mais antigas (geração Midgard) não expõem no OpenGL ES, como shaders de geometria e tesselação. Nesses aparelhos o OpenGL pode não renderizar certo esses jogos; nesse caso use Vulkan.
