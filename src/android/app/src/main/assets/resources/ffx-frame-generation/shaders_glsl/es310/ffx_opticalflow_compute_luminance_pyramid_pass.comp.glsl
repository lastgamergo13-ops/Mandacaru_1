#version 310 es
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

float _68;

layout(binding = 0, r32ui) uniform readonly highp uimage2D rw_optical_flow_input;
layout(binding = 1, r32ui) uniform coherent writeonly highp uimage2D rw_optical_flow_input_level_1;
layout(binding = 2, r32ui) uniform coherent writeonly highp uimage2D rw_optical_flow_input_level_2;
layout(binding = 3, r32ui) uniform coherent writeonly highp uimage2D rw_optical_flow_input_level_3;
layout(binding = 4, r32ui) uniform coherent writeonly highp uimage2D rw_optical_flow_input_level_4;
layout(binding = 5, r32ui) uniform coherent writeonly highp uimage2D rw_optical_flow_input_level_5;
layout(binding = 6, r32ui) uniform coherent writeonly highp uimage2D rw_optical_flow_input_level_6;

shared float spdIntermediateR[16][16];
shared float spdIntermediateG[16][16];
shared float spdIntermediateB[16][16];
shared float spdIntermediateA[16][16];

void main()
{
    uvec2 _74 = uvec2(ivec3(gl_WorkGroupID).xy);
    uint _75 = uint(int(gl_LocalInvocationIndex));
    uint _76 = _75 % 64u;
    uint _92 = ((_76 & 1u) | (((_76 >> 2u) & 7u) & 4294967294u)) + (8u * ((_75 >> 6u) % 2u));
    uint _95 = ((((_76 >> 1u) & 3u) & 3u) | (((_76 >> 3u) & 7u) & 4294967292u)) + (8u * (_75 >> 7u));
    ivec2 _97 = ivec2(_74 * uvec2(64u));
    uint _98 = _92 * 2u;
    int _99 = int(_98);
    uint _100 = _95 * 2u;
    int _101 = int(_100);
    ivec2 _105 = ivec2(_74 * uvec2(32u));
    int _106 = int(_92);
    int _107 = int(_95);
    ivec2 _108 = ivec2(_106, _107);
    uvec2 _110 = uvec2(_97 + ivec2(_99, _101));
    vec4 _70[4];
    _70[0] = (((vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_110)))))).x), 0.0, 0.0, 0.0) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_110 + uvec2(0u, 1u))))))).x), 0.0, 0.0, 0.0)) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_110 + uvec2(1u, 0u))))))).x), 0.0, 0.0, 0.0)) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_110 + uvec2(1u))))))).x), 0.0, 0.0, 0.0)) * 0.25;
    imageStore(rw_optical_flow_input_level_1, ivec2(uvec2(_105 + _108)), uvec4(uint(_70[0].x)));
    int _161 = int(_98 + 32u);
    int _165 = int(_92 + 16u);
    uvec2 _168 = uvec2(_97 + ivec2(_161, _101));
    _70[1] = (((vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_168)))))).x), 0.0, 0.0, 0.0) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_168 + uvec2(0u, 1u))))))).x), 0.0, 0.0, 0.0)) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_168 + uvec2(1u, 0u))))))).x), 0.0, 0.0, 0.0)) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_168 + uvec2(1u))))))).x), 0.0, 0.0, 0.0)) * 0.25;
    imageStore(rw_optical_flow_input_level_1, ivec2(uvec2(_105 + ivec2(_165, _107))), uvec4(uint(_70[1].x)));
    int _219 = int(_100 + 32u);
    int _223 = int(_95 + 16u);
    uvec2 _226 = uvec2(_97 + ivec2(_99, _219));
    _70[2] = (((vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_226)))))).x), 0.0, 0.0, 0.0) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_226 + uvec2(0u, 1u))))))).x), 0.0, 0.0, 0.0)) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_226 + uvec2(1u, 0u))))))).x), 0.0, 0.0, 0.0)) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_226 + uvec2(1u))))))).x), 0.0, 0.0, 0.0)) * 0.25;
    imageStore(rw_optical_flow_input_level_1, ivec2(uvec2(_105 + ivec2(_106, _223))), uvec4(uint(_70[2].x)));
    uvec2 _280 = uvec2(_97 + ivec2(_161, _219));
    _70[3] = (((vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_280)))))).x), 0.0, 0.0, 0.0) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_280 + uvec2(0u, 1u))))))).x), 0.0, 0.0, 0.0)) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_280 + uvec2(1u, 0u))))))).x), 0.0, 0.0, 0.0)) + vec4(float(imageLoad(rw_optical_flow_input, ivec2(uvec2(ivec2(vec2(ivec2(_280 + uvec2(1u))))))).x), 0.0, 0.0, 0.0)) * 0.25;
    imageStore(rw_optical_flow_input_level_1, ivec2(uvec2(_105 + ivec2(_165, _223))), uvec4(uint(_70[3].x)));
    for (uint _331 = 0u; _331 < 4u; _331++)
    {
        spdIntermediateR[_92][_95] = _70[_331].x;
        spdIntermediateG[_92][_95] = _70[_331].y;
        spdIntermediateB[_92][_95] = _70[_331].z;
        spdIntermediateA[_92][_95] = _70[_331].w;
        barrier();
        if (_75 < 64u)
        {
            uint _350 = _98 + 1u;
            uint _351 = _100 + 1u;
            _70[_331] = (((vec4(spdIntermediateR[_98][_100], spdIntermediateG[_98][_100], spdIntermediateB[_98][_100], spdIntermediateA[_98][_100]) + vec4(spdIntermediateR[_350][_100], spdIntermediateG[_350][_100], spdIntermediateB[_350][_100], spdIntermediateA[_350][_100])) + vec4(spdIntermediateR[_98][_351], spdIntermediateG[_98][_351], spdIntermediateB[_98][_351], spdIntermediateA[_98][_351])) + vec4(spdIntermediateR[_350][_351], spdIntermediateG[_350][_351], spdIntermediateB[_350][_351], spdIntermediateA[_350][_351])) * 0.25;
            imageStore(rw_optical_flow_input_level_2, ivec2(uvec2(ivec2(_74 * uvec2(16u)) + ivec2(int(_92 + ((_331 % 2u) * 8u)), int(_95 + ((_331 / 2u) * 8u))))), uvec4(uint(_70[_331].x)));
        }
        barrier();
    }
    bool _409 = _75 < 64u;
    if (_409)
    {
        spdIntermediateR[_92][_95] = _70[0].x;
        spdIntermediateG[_92][_95] = _70[0].y;
        spdIntermediateB[_92][_95] = _70[0].z;
        spdIntermediateA[_92][_95] = _70[0].w;
        uint _421 = _92 + 8u;
        spdIntermediateR[_421][_95] = _70[1].x;
        spdIntermediateG[_421][_95] = _70[1].y;
        spdIntermediateB[_421][_95] = _70[1].z;
        spdIntermediateA[_421][_95] = _70[1].w;
        uint _431 = _95 + 8u;
        spdIntermediateR[_92][_431] = _70[2].x;
        spdIntermediateG[_92][_431] = _70[2].y;
        spdIntermediateB[_92][_431] = _70[2].z;
        spdIntermediateA[_92][_431] = _70[2].w;
        spdIntermediateR[_421][_431] = _70[3].x;
        spdIntermediateG[_421][_431] = _70[3].y;
        spdIntermediateB[_421][_431] = _70[3].z;
        spdIntermediateA[_421][_431] = _70[3].w;
    }
    barrier();
    if (_409)
    {
        uint _452 = _98 + 1u;
        uint _453 = _100 + 1u;
        float _455 = spdIntermediateR[_98][_100];
        float _457 = spdIntermediateG[_98][_100];
        float _459 = spdIntermediateB[_98][_100];
        float _464 = spdIntermediateR[_452][_100];
        float _466 = spdIntermediateG[_452][_100];
        float _468 = spdIntermediateB[_452][_100];
        float _473 = spdIntermediateR[_98][_453];
        float _475 = spdIntermediateG[_98][_453];
        float _477 = spdIntermediateB[_98][_453];
        float _482 = spdIntermediateR[_452][_453];
        float _484 = spdIntermediateG[_452][_453];
        float _486 = spdIntermediateB[_452][_453];
        vec4 _493 = (((vec4(_455, _457, _459, spdIntermediateA[_98][_100]) + vec4(_464, _466, _468, spdIntermediateA[_452][_100])) + vec4(_473, _475, _477, spdIntermediateA[_98][_453])) + vec4(_482, _484, _486, spdIntermediateA[_452][_453])) * 0.25;
        float _497 = _493.x;
        imageStore(rw_optical_flow_input_level_3, ivec2(uvec2(ivec2(_74 * uvec2(8u)) + _108)), uvec4(uint(_497)));
        uint _502 = _98 + (_95 % 2u);
        spdIntermediateR[_502][_100] = _497;
        spdIntermediateG[_502][_100] = _493.y;
        spdIntermediateB[_502][_100] = _493.z;
        spdIntermediateA[_502][_100] = _493.w;
    }
    barrier();
    if (_75 < 16u)
    {
        uint _513 = _92 * 4u;
        uint _514 = _95 * 4u;
        uint _515 = _513 + 2u;
        uint _516 = _513 + 1u;
        uint _517 = _514 + 2u;
        uint _518 = _513 + 3u;
        float _520 = spdIntermediateR[_513][_514];
        float _522 = spdIntermediateG[_513][_514];
        float _524 = spdIntermediateB[_513][_514];
        float _529 = spdIntermediateR[_515][_514];
        float _531 = spdIntermediateG[_515][_514];
        float _533 = spdIntermediateB[_515][_514];
        float _538 = spdIntermediateR[_516][_517];
        float _540 = spdIntermediateG[_516][_517];
        float _542 = spdIntermediateB[_516][_517];
        float _547 = spdIntermediateR[_518][_517];
        float _549 = spdIntermediateG[_518][_517];
        float _551 = spdIntermediateB[_518][_517];
        vec4 _558 = (((vec4(_520, _522, _524, spdIntermediateA[_513][_514]) + vec4(_529, _531, _533, spdIntermediateA[_515][_514])) + vec4(_538, _540, _542, spdIntermediateA[_516][_517])) + vec4(_547, _549, _551, spdIntermediateA[_518][_517])) * 0.25;
        float _562 = _558.x;
        imageStore(rw_optical_flow_input_level_4, ivec2(uvec2(ivec2(_74 * uvec2(4u)) + _108)), uvec4(uint(_562)));
        uint _566 = _513 + _95;
        spdIntermediateR[_566][_514] = _562;
        spdIntermediateG[_566][_514] = _558.y;
        spdIntermediateB[_566][_514] = _558.z;
        spdIntermediateA[_566][_514] = _558.w;
    }
    barrier();
    if (_75 < 4u)
    {
        uint _577 = _92 * 8u;
        uint _578 = _577 + _100;
        uint _579 = _95 * 8u;
        uint _581 = (_577 + 4u) + _100;
        uint _583 = (_577 + 1u) + _100;
        uint _584 = _579 + 4u;
        uint _586 = (_577 + 5u) + _100;
        float _588 = spdIntermediateR[_578][_579];
        float _590 = spdIntermediateG[_578][_579];
        float _592 = spdIntermediateB[_578][_579];
        float _597 = spdIntermediateR[_581][_579];
        float _599 = spdIntermediateG[_581][_579];
        float _601 = spdIntermediateB[_581][_579];
        float _606 = spdIntermediateR[_583][_584];
        float _608 = spdIntermediateG[_583][_584];
        float _610 = spdIntermediateB[_583][_584];
        float _615 = spdIntermediateR[_586][_584];
        float _617 = spdIntermediateG[_586][_584];
        float _619 = spdIntermediateB[_586][_584];
        vec4 _626 = (((vec4(_588, _590, _592, spdIntermediateA[_578][_579]) + vec4(_597, _599, _601, spdIntermediateA[_581][_579])) + vec4(_606, _608, _610, spdIntermediateA[_583][_584])) + vec4(_615, _617, _619, spdIntermediateA[_586][_584])) * 0.25;
        float _630 = _626.x;
        imageStore(rw_optical_flow_input_level_5, ivec2(uvec2(ivec2(_74 * uvec2(2u)) + _108)), uvec4(uint(_630)));
        uint _634 = _92 + _100;
        spdIntermediateR[_634][0u] = _630;
        spdIntermediateG[_634][0u] = _626.y;
        spdIntermediateB[_634][0u] = _626.z;
        spdIntermediateA[_634][0u] = _626.w;
    }
    barrier();
    if (_75 < 1u)
    {
        imageStore(rw_optical_flow_input_level_6, ivec2(uvec2(ivec2(_74))), uvec4(uint(((((vec4(spdIntermediateR[0u][0u], _68, _68, _68) + vec4(spdIntermediateR[1u][0u], _68, _68, _68)) + vec4(spdIntermediateR[2u][0u], _68, _68, _68)) + vec4(spdIntermediateR[3u][0u], _68, _68, _68)) * 0.25).x)));
    }
}

