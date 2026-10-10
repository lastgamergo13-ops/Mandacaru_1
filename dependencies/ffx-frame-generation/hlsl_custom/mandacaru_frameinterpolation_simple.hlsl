// mandacaru_frameinterpolation_simple.hlsl
//
// Interpolacao de frame SIMPLES, escrita para o Cemu Android, que usa o
// fluxo optico REAL do AMD FSR3 (OpticalFlowVk) como unica fonte de
// movimento. O Cemu nao tem depth nem motion vectors do jogo, que o
// Frame Interpolation original da AMD exige - por isso este substituto.
//
// Sem sampler, sem quad ops, sem WriteWithoutFormat: so Load() +
// bilinear manual. Um unico dispatch.
//
// Bindings (com -fvk-b-shift 0 -fvk-t-shift 100 -fvk-u-shift 200):
//   b0   -> 0   cbInterp
//   t0   -> 100 r_prev   (frame real anterior, RGBA/BGRA 8 bits)
//   t1   -> 101 r_cur    (frame real atual)
//   t2   -> 102 r_flow   (R32G32_SINT, ceil(w/8) x ceil(h/8), pixels cheios)
//   u0   -> 200 rw_out   (RGBA16F)

Texture2D<float4> r_prev : register(t0);
Texture2D<float4> r_cur  : register(t1);
Texture2D<int2>   r_flow : register(t2);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> rw_out : register(u0);

cbuffer cbInterp : register(b0)
{
    int2  iSize;          // resolucao dos frames
    int2  iFlowSize;      // ceil(w/8), ceil(h/8)
    float fFlowSign;      // +1: vetor aponta do pixel do frame ATUAL para o ANTERIOR
    float fFlowScale;     // multiplicador do vetor (1.0 = pixels)
    float fT;             // posicao temporal do frame gerado (0.5 = meio)
    float fErrThreshold;  // erro de cor que ja considera o warp nao confiavel
    int   iBlock;         // 8
    int3  _pad;
};

float4 FetchClamped(Texture2D<float4> tex, int2 p)
{
    p = clamp(p, int2(0, 0), iSize - 1);
    return tex.Load(int3(p, 0));
}

float4 SampleBilinear(Texture2D<float4> tex, float2 pos)
{
    // pos em coordenadas de pixel (centro do pixel = +0.5)
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
    float2 f = pixelPos / (float)iBlock - 0.5;
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

    // Objeto em m (frame gerado, tempo t entre prev(0) e cur(1)):
    //   no anterior esta em m + t*v ; no atual esta em m - (1-t)*v
    float4 cPrev = SampleBilinear(r_prev, center + v * fT);
    float4 cCur  = SampleBilinear(r_cur,  center - v * (1.0 - fT));

    float4 warped = lerp(cPrev, cCur, fT);
    float4 plain  = lerp(FetchClamped(r_prev, p), FetchClamped(r_cur, p), fT);

    float3 d = abs(cPrev.rgb - cCur.rgb);
    float err = max(d.r, max(d.g, d.b));
    float k = saturate((err - fErrThreshold) / max(fErrThreshold, 1e-4));

    float4 outc = lerp(warped, plain, k);
    outc.a = 1.0;
    rw_out[p] = outc;
}
