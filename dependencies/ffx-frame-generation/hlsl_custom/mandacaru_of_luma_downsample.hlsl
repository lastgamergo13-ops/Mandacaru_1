// Downsample 2x2 da luma (uint 0..255) para a piramide do Optical Flow (Mandacaru).
Texture2D<uint>   r_src  : register(t0);
RWTexture2D<uint> rw_dst : register(u0);

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    uint dw, dh, sw, sh;
    rw_dst.GetDimensions(dw, dh);
    r_src.GetDimensions(sw, sh);
    if (id.x >= dw || id.y >= dh)
        return;
    int2 p = int2(id.xy) * 2;
    int2 mx = int2(sw, sh) - 1;
    uint a = r_src.Load(int3(min(p + int2(0, 0), mx), 0));
    uint b = r_src.Load(int3(min(p + int2(1, 0), mx), 0));
    uint c = r_src.Load(int3(min(p + int2(0, 1), mx), 0));
    uint d = r_src.Load(int3(min(p + int2(1, 1), mx), 0));
    rw_dst[id.xy] = (a + b + c + d + 2) / 4;
}
