// mandacaru_frameinterpolation_v8.hlsl
//
// V8 = V7 + RECUPERACAO DE OCLUSAO (equivalente simples dos passes "dilate / disocclusion / inpainting"
// da AMD, que dependem de depth e motion vectors do jogo - o Cemu nao os tem).
//   Quando o vetor do pixel NAO e confiavel (cor puxada de "prev" difere muito da cor atual - tipico de
//   bordas de objetos que se movem e de areas descobertas), em vez de cair direto no pixel "puro"
//   (que causa fantasma/serrilhado), testa os vetores dos 8 vizinhos do fluxo e usa o que tiver MENOR
//   erro de cor. So se nenhum servir cai no pixel puro. Roda so nos pixels duvidosos (custo baixo).
// Mesmo cbuffer (48 bytes) e mesmos bindings da V7.
//
// Compilar:
//   dxc.exe -spirv "-fspv-target-env=vulkan1.1" -T cs_6_0 -E CS -fvk-b-shift 0 0 -fvk-t-shift 100 0 -fvk-u-shift 200 0 mandacaru_frameinterpolation_v8.hlsl -Fo mandacaru_frameinterpolation_v8.spv
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

float4 LoadAt(Texture2D<float4> tex, int2 p)
{
    return tex.Load(int3(clamp(p, int2(0, 0), iSize - 1), 0));
}

float ColorErr(float3 a, float3 b)
{
    float3 d = abs(a - b);
    return max(d.r, max(d.g, d.b));
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)iSize.x || id.y >= (uint)iSize.y)
        return;

    int2 p = (int2)id.xy;
    float2 center = (float2)p + 0.5;
    float2 texel = (float2)iSize / (float2)iFlowSize;

    float2 v = SampleFlow(center) * fFlowScale * fFlowSign;
    float4 outc;

    if (fT > 1.0)
    {
        float s = fT - 1.0;
        float4 plain = FetchClamped(r_cur, p);
        float4 cPrev = SampleBilinear(r_prev, center + v);
        float err = ColorErr(cPrev.rgb, plain.rgb);

        if (err > fErrThreshold)
        {
            // vetor duvidoso: procura entre os vizinhos do fluxo o de menor erro
            int2 g0 = (int2)floor(center / texel);
            float bestErr = err;
            float2 bestV = v;
            [unroll] for (int dy = -1; dy <= 1; dy++)
            {
                [unroll] for (int dx = -1; dx <= 1; dx++)
                {
                    float2 vn = FlowAt(g0 + int2(dx, dy)) * fFlowScale * fFlowSign;
                    float e = ColorErr(LoadAt(r_prev, p + (int2)round(vn)).rgb, plain.rgb);
                    if (e < bestErr) { bestErr = e; bestV = vn; }
                }
            }
            v = bestV;
            err = bestErr;
        }

        float4 cCur = SampleBilinear(r_cur, center + v * s);
        float k = saturate((err - fErrThreshold) / max(fErrThreshold, 1e-4));
        outc = lerp(cCur, plain, k);
    }
    else
    {
        float4 cPrev = SampleBilinear(r_prev, center + v * fT);
        float4 cCur  = SampleBilinear(r_cur,  center - v * (1.0 - fT));
        float4 plain = lerp(FetchClamped(r_prev, p), FetchClamped(r_cur, p), fT);
        float err = ColorErr(cPrev.rgb, cCur.rgb);

        if (err > fErrThreshold)
        {
            int2 g0 = (int2)floor(center / texel);
            float bestErr = err;
            float4 bestW = lerp(cPrev, cCur, fT);
            [unroll] for (int dy = -1; dy <= 1; dy++)
            {
                [unroll] for (int dx = -1; dx <= 1; dx++)
                {
                    float2 vn = FlowAt(g0 + int2(dx, dy)) * fFlowScale * fFlowSign;
                    float4 a = LoadAt(r_prev, p + (int2)round(vn * fT));
                    float4 b = LoadAt(r_cur,  p - (int2)round(vn * (1.0 - fT)));
                    float e = ColorErr(a.rgb, b.rgb);
                    if (e < bestErr) { bestErr = e; bestW = lerp(a, b, fT); }
                }
            }
            err = bestErr;
            cPrev = bestW; // reaproveita a variavel como "cor warpada escolhida"
            float k = saturate((err - fErrThreshold) / max(fErrThreshold, 1e-4));
            outc = lerp(bestW, plain, k);
        }
        else
        {
            outc = lerp(lerp(cPrev, cCur, fT), plain, 0.0);
        }
    }

    outc.a = 1.0;
    rw_out[p] = outc;
}
