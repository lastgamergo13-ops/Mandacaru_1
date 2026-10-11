// mandacaru_frameinterpolation_v7.hlsl
//
// Geracao de frame para o Cemu Android, usando o fluxo optico REAL do AMD FSR3 (OpticalFlowVk).
// O Cemu nao tem depth nem motion vectors do jogo - por isso este shader proprio.
//
// NOVO NA V7 (em relacao a mandacaru_frameinterpolation_simple):
//  1) Dois modos, escolhidos por fT:
//       fT <= 1  -> INTERPOLACAO entre prev (t=0) e cur (t=1)   [igual a V6]
//       fT >  1  -> EXTRAPOLACAO: frame em "cur + (fT-1)" do intervalo, a partir do frame
//                   atual. Mostrar isso DEPOIS do frame real atual mantem a ordem no tempo
//                   (o frame real ja foi apresentado antes de gerarmos o extra).
//  2) Flow scale: o Optical Flow pode rodar em resolucao menor que a tela. O tamanho do
//     texel do fluxo em pixels cheios = iSize / iFlowSize (nao mais "/8" fixo) e
//     fFlowScale = largura_cheia / largura_do_optical_flow converte o vetor para pixels cheios.
//
// O cbuffer tem o MESMO layout (48 bytes) da V6.
//
// Compilar (igual ao anterior):
//   dxc.exe -spirv "-fspv-target-env=vulkan1.1" -T cs_6_0 -E CS -fvk-b-shift 0 0 -fvk-t-shift 100 0 -fvk-u-shift 200 0 mandacaru_frameinterpolation_v7.hlsl -Fo mandacaru_frameinterpolation_v7.spv
// Bindings: b0->0 cbInterp | t0->100 r_prev | t1->101 r_cur | t2->102 r_flow | u0->200 rw_out

Texture2D<float4> r_prev : register(t0);
Texture2D<float4> r_cur  : register(t1);
Texture2D<int2>   r_flow : register(t2);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> rw_out : register(u0);

cbuffer cbInterp : register(b0)
{
    int2  iSize;          // resolucao dos frames (cheia)
    int2  iFlowSize;      // tamanho da textura de fluxo: ceil(ofW/8), ceil(ofH/8)
    float fFlowSign;      // +1: vetor aponta do pixel do frame ATUAL para o ANTERIOR
    float fFlowScale;     // converte vetor (pixels do optical flow) em pixels cheios
    float fT;             // <=1 interpola (0.5 = meio); >1 extrapola (1+s)
    float fErrThreshold;  // erro de cor que ja considera o warp nao confiavel
    int   iBlock;         // (nao usado na V7 - mantido pelo layout)
    int3  _pad;
};

float4 FetchClamped(Texture2D<float4> tex, int2 p)
{
    p = clamp(p, int2(0, 0), iSize - 1);
    return tex.Load(int3(p, 0));
}

float4 SampleBilinear(Texture2D<float4> tex, float2 pos)
{
    float2 f = pos - 0.5;
    int2 i0 = (int2)floor(f);
    float2 w = f - (float2)i0;
    float4 a = FetchClamped(tex, i0);
    float4 b = FetchClamped(tex, i0 + int2(1, 0));
    float4 c = FetchClamped(tex, i0 + int2(0, 1));
    float4 d = FetchClamped(tex, i0 + int2(1, 1));
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}

float2 FlowAt(int2 g)
{
    g = clamp(g, int2(0, 0), iFlowSize - 1);
    return (float2)r_flow.Load(int3(g, 0));
}

float2 SampleFlow(float2 pixelPos)
{
    // tamanho (em pixels cheios) de um texel do fluxo
    float2 texel = (float2)iSize / (float2)iFlowSize;
    float2 f = pixelPos / texel - 0.5;
    int2 i0 = (int2)floor(f);
    float2 w = f - (float2)i0;
    float2 a = FlowAt(i0);
    float2 b = FlowAt(i0 + int2(1, 0));
    float2 c = FlowAt(i0 + int2(0, 1));
    float2 d = FlowAt(i0 + int2(1, 1));
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)iSize.x || id.y >= (uint)iSize.y)
        return;

    int2 p = (int2)id.xy;
    float2 center = (float2)p + 0.5;

    float2 v = SampleFlow(center) * fFlowScale * fFlowSign;
    float4 outc;

    if (fT > 1.0)
    {
        // EXTRAPOLACAO: o pixel m no instante 1+s veio de cur em m + s*v (v aponta cur->prev).
        float s = fT - 1.0;
        float4 cCur  = SampleBilinear(r_cur,  center + v * s);
        float4 plain = FetchClamped(r_cur, p);
        // consistencia: o pixel de cur em 'center' deve existir em prev em center+v
        float4 cPrev = SampleBilinear(r_prev, center + v);
        float3 d = abs(cPrev.rgb - plain.rgb);
        float err = max(d.r, max(d.g, d.b));
        float k = saturate((err - fErrThreshold) / max(fErrThreshold, 1e-4));
        outc = lerp(cCur, plain, k);
    }
    else
    {
        // INTERPOLACAO (igual a V6)
        float4 cPrev = SampleBilinear(r_prev, center + v * fT);
        float4 cCur  = SampleBilinear(r_cur,  center - v * (1.0 - fT));
        float4 warped = lerp(cPrev, cCur, fT);
        float4 plain  = lerp(FetchClamped(r_prev, p), FetchClamped(r_cur, p), fT);
        float3 d = abs(cPrev.rgb - cCur.rgb);
        float err = max(d.r, max(d.g, d.b));
        float k = saturate((err - fErrThreshold) / max(fErrThreshold, 1e-4));
        outc = lerp(warped, plain, k);
    }

    outc.a = 1.0;
    rw_out[p] = outc;
}
