# dependencies/ffx-frame-generation/CMakeLists.txt
#
# Compila o backend Vulkan "escrito na mao" do FSR3 Frame Generation
# como uma static lib, pra ser linkada no alvo principal do Cemu.
#
# Os .spv em shaders_spv/ NAO sao compilados aqui - ja vem prontos
# (gerados via dxc.exe, fora do CMake). Eles sao copiados para o
# diretorio de saida do build para serem carregados em runtime.

add_library(ffxFrameGeneration STATIC
	src/opticalflow_vk.cpp
	src/opticalflow_vk.h
)

target_include_directories(ffxFrameGeneration PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

# O projeto raiz ja faz include_directories("dependencies/Vulkan-Headers/include")
# globalmente (ver CMakeLists.txt raiz, linha ~248) - nao existe um
# target CMake pra isso neste projeto, entao nao precisa linkar nada
# aqui, so garantir que este target compila DEPOIS que esse include
# global ja foi declarado (ele e declarado antes de qualquer
# add_subdirectory de dependencies/, entao esta tudo certo).

# Copia os .spv para perto do executavel final, para LoadShaderModule()
# encontrar via caminho relativo em runtime.
add_custom_command(TARGET ffxFrameGeneration POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/shaders_spv
		$<TARGET_FILE_DIR:ffxFrameGeneration>/dependencies/ffx-frame-generation/shaders_spv
)
