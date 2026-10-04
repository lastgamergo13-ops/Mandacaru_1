// Substituto SIMPLIFICADO do pass original da AMD (ffx_opticalflow_compute_luminance_pyramid_pass.hlsl).
//
// POR QUE ESSE ARQUIVO EXISTE: o shader original da AMD usa o
// algoritmo SPD (Single Pass Downsampler), que internamente chama
// operacoes de quad (OpGroupNonUniformQuadSwap) mesmo com a flag
// FFX_SPD_NO_WAVE_OPERATIONS definida - confirmado via disassembly
// real do .spv, nao so leitura do .hlsl. Isso exige a extensao
// SPV_KHR_compute_shader_derivatives, que o driver do Adreno 650 nao
// suporta, quebrando a criacao do pipeline.
//
// Como nossa versao do Optical Flow ja simplifica pra "1 nivel real"
// (niveis 2-6 sao so imagens dummy, sem uso funcional - ver aviso no
// opticalflow_vk.h), nao precisamos do algoritmo SPD sofisticado da
// AMD aqui. Este substituto faz um downsample 2x2 simples (media de 4
// texels), sem nenhuma operacao de quad/wave - os MESMOS 7 bindings
// do original, pra nao precisar mudar nada no C++.
//
// Bindings (shift ja aplicado: u+200, sem constant buffer - igual ao original):
//   200: rw_optical_flow_input          (entrada, leitura)
//   201: rw_optical_flow_input_level_1  (saida real - downsample 2x2 de verdade)
//   202-206: levels 2-6 (dummy - so escreve um valor constante, sem uso funcional)

// [[vk::image_format]] forca o formato certo (1 canal) - sem isso o
// dxc assume Rgba32f (4 canais) por padrao pra RWTexture2D<float>,
// o que nao bate com a imagem real criada no C++ (VK_FORMAT_R32_SFLOAT).
[[vk::image_format("r32f")]] RWTexture2D<float> rw_optical_flow_input          : register(u0);
[[vk::image_format("r32f")]] RWTexture2D<float> rw_optical_flow_input_level_1  : register(u1);
[[vk::image_format("r32f")]] RWTexture2D<float> rw_optical_flow_input_level_2  : register(u2);
[[vk::image_format("r32f")]] RWTexture2D<float> rw_optical_flow_input_level_3  : register(u3);
[[vk::image_format("r32f")]] RWTexture2D<float> rw_optical_flow_input_level_4  : register(u4);
[[vk::image_format("r32f")]] RWTexture2D<float> rw_optical_flow_input_level_5  : register(u5);
[[vk::image_format("r32f")]] RWTexture2D<float> rw_optical_flow_input_level_6  : register(u6);

[numthreads(16, 16, 1)]
void CS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 outPos = dispatchThreadId.xy;

    // Downsample 2x2 simples (media de 4 texels) - sem quad swap,
    // sem subgroup, sem SPD. So leitura direta + media aritmetica.
    uint2 inPos = outPos * 2;
    float a = rw_optical_flow_input[inPos + uint2(0, 0)];
    float b = rw_optical_flow_input[inPos + uint2(1, 0)];
    float c = rw_optical_flow_input[inPos + uint2(0, 1)];
    float d = rw_optical_flow_input[inPos + uint2(1, 1)];
    float avg = (a + b + c + d) * 0.25;

    rw_optical_flow_input_level_1[outPos] = avg;

    // Niveis 2-6 sao dummy nesta versao simplificada - so garante que
    // a imagem (4x4, ver CreateIntermediateResources) tenha ALGUM
    // valor definido, sem custo nenhum de performance real.
    if (outPos.x < 4 && outPos.y < 4)
    {
        rw_optical_flow_input_level_2[outPos] = avg;
        rw_optical_flow_input_level_3[outPos] = avg;
        rw_optical_flow_input_level_4[outPos] = avg;
        rw_optical_flow_input_level_5[outPos] = avg;
        rw_optical_flow_input_level_6[outPos] = avg;
    }
}
